#include <logging.h>
#include <kernel/vmm.h>
#include <arch/mmu.h>
#include <kernel/alloc.h>
#include <kernel/interrupt.h>
#include <arch/cpu.h>
#include <arch/hpet.h>
#include <arch/msr.h>
#include <kernel/timer.h>
#include <kernel/timekeeper.h>
#include <kernel/init.h>

#include <uacpi/tables.h>
#include <uacpi/acpi.h>

#define IOAPIC_REG_ID 0
#define IOAPIC_REG_ENTRY_COUNT 1
#define IOAPIC_REG_PRIORITY 2
#define IOAPIC_REG_ENTRY 0x10

#define APIC_REG_ID 0x20
#define APIC_REG_EOI 0xB0
#define APIC_REG_SPURIOUS 0xF0
#define APIC_REG_ICR_LO 0x300
#define APIC_REG_ICR_LO_STATUS (1 << 12)
#define APIC_REG_ICR_HI 0x310
#define APIC_LVT_TIMER 0x320
#define APIC_LVT_THERMAL 0x330
#define APIC_LVT_PERFORMANCE 0x340
#define APIC_LVT_LINT0 0x350
#define APIC_LVT_LINT1 0x360
#define APIC_LVT_ERROR 0x370
#define APIC_TIMER_INITIAL_COUNT 0x380
#define APIC_TIMER_COUNT 0x390
#define APIC_TIMER_DIVIDE 0x3E0

#define LVT_DELIVERY_NMI (0b100 << 8)
#define LVT_MASK (1 << 16)

typedef struct {
	void *addr;
	int base;
	int top;
} ioapic_desc_t;

static ioapic_desc_t *ioapics;

static struct acpi_madt *madt;
static struct acpi_entry_hdr *list_start;
static struct acpi_entry_hdr *list_end;
static void *lapic_address;

static size_t override_count;
static size_t io_count;
static size_t lapic_count;
static size_t lapic_nmi_count;

static ioapic_desc_t *ioapicfromgsi(int gsi) {
	for (int i = 0; i < io_count; ++i) {
		if (ioapics[i].base <= gsi && ioapics[i].top > gsi)
			return &ioapics[i];
	}
	return NULL;
}

static inline struct acpi_entry_hdr *getnext(struct acpi_entry_hdr *header) {
	uintptr_t ptr = (uintptr_t)header;
	return (struct acpi_entry_hdr *)(ptr + header->length);
}

static inline void *getentry(int type, int n) {
	struct acpi_entry_hdr *entry = list_start;

	while (entry < list_end) {
		if (entry->type == type) {
			if (n-- == 0)
				return entry;
		}
		entry = getnext(entry);
	}

	return NULL;
}

static int getcount(int type) {
	struct acpi_entry_hdr *entry = list_start;
	int count = 0;

	while (entry < list_end) {
		if (entry->type == type)
			++count;
		entry = getnext(entry);
	}

	return count;
}

static inline void writelapic(int reg, uint32_t v) {
	volatile uint32_t *ptr = (void *)((uintptr_t)lapic_address + reg);
	*ptr = v;
}

static inline uint32_t readlapic(int reg) {
	volatile uint32_t *ptr = (void *)((uintptr_t)lapic_address + reg);
	return *ptr;
}

static uint32_t readioapic(void *ioapic, int reg) {
	volatile uint32_t *apic = ioapic;
	*apic = reg & 0xff;
	return *(apic + 4);
}

static void writeioapic(void *ioapic, int reg, uint32_t v) {
	volatile uint32_t *apic = ioapic;
	*apic = reg & 0xff;
	*(apic + 4) = v;
}

static void writeiored(void *ioapic, uint8_t entry, uint8_t vector, uint8_t delivery, uint8_t destmode, uint8_t polarity, uint8_t mode, uint8_t mask, uint8_t dest){
	uint32_t val = vector;
	val |= (delivery & 0b111) << 8;
	val |= (destmode & 1) << 11;
	val |= (polarity & 1) << 13;
	val |= (mode & 1) << 15;
	val |= (mask & 1) << 16;

	writeioapic(ioapic, 0x10 + entry * 2, val);
	writeioapic(ioapic, 0x11 + entry * 2, (uint32_t)dest << 24);
}

void arch_ioapic_setirq(uint8_t irq, uint8_t vector, uint8_t proc, bool masked) {
	// default settings for ISA irqs
	uint8_t polarity = 1; // active high
	uint8_t trigger  = 0; // edge triggered
	for (uintmax_t i = 0; i < override_count; ++i) {
		struct acpi_madt_interrupt_source_override *override = getentry(ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE, i);
		if (override->source != irq)
			continue;

		polarity = override->flags & 2 ? 1 : 0; // active low
		trigger  = override->flags & 8 ? 1 : 0; // level triggered
		irq = override->gsi;
		break;
	}

	ioapic_desc_t* ioapic = ioapicfromgsi(irq);
	__assert(ioapic);
	irq = irq - ioapic->base;
	writeiored(ioapic->addr, irq, vector, 0, 0, polarity, trigger, masked ? 1 : 0, proc);
}

static void spurious(isr_t *self, context_t *ctx) {
	__assert(!"Spurious interrupt");
}

static void nmi(isr_t *self, context_t *ctx) {
	__assert(!"NMI");
}

void arch_apic_initap() {
	writelapic(APIC_LVT_THERMAL, LVT_MASK);
	writelapic(APIC_LVT_PERFORMANCE, LVT_MASK);
	writelapic(APIC_LVT_LINT0, LVT_MASK);
	writelapic(APIC_LVT_LINT1, LVT_MASK);
	writelapic(APIC_LVT_ERROR, LVT_MASK);
	writelapic(APIC_LVT_TIMER, LVT_MASK);
	interrupt_register(0xff, spurious, NULL, IPL_IGNORE);
	writelapic(APIC_REG_SPURIOUS, 0x1FF);

	current_cpu()->hardware_id = readlapic(APIC_REG_ID) >> 24;

	// get the acpi id for the local nmi sources

	for (size_t i = 0; i < lapic_count; ++i) {
		struct acpi_madt_lapic *current = getentry(ACPI_MADT_ENTRY_TYPE_LAPIC, i);
		if (current_cpu()->hardware_id == current->id) {
			current_cpu()->acpiid = current->uid;
			break;
		}
	}

	isr_t *nmiisr = interrupt_allocate(nmi, NULL, IPL_MAX); // MAX IPL because NMI
	__assert(nmiisr);

	for (size_t i = 0; i < lapic_nmi_count; ++i) {
		struct acpi_madt_lapic_nmi* current = getentry(ACPI_MADT_ENTRY_TYPE_LAPIC_NMI, i);
		if (current->uid == current_cpu()->acpiid || current->uid == 0xff)
			writelapic(APIC_LVT_LINT0 + 0x10 * current->lint, (nmiisr->id & 0xff) | LVT_DELIVERY_NMI | (current->flags << 12));
	}
}

void arch_apic_eoi() {
	writelapic(APIC_REG_EOI, 0);
}

static time_t stoptimer(timer_t *) {
	time_t remaining = readlapic(APIC_TIMER_COUNT);
	time_t initial = readlapic(APIC_TIMER_INITIAL_COUNT);

	writelapic(APIC_TIMER_INITIAL_COUNT, 0);

	return initial - remaining;
}

static time_t time_passed(timer_t *) {
	time_t remaining = readlapic(APIC_TIMER_COUNT);
	time_t initial = readlapic(APIC_TIMER_INITIAL_COUNT);

	return initial - remaining;
}

static void timerisr(isr_t *isr, context_t *context) {
	timer_isr(current_cpu()->timer, context);
}

static void armtimer(timer_t *, time_t ticks) {
	writelapic(APIC_TIMER_INITIAL_COUNT, ticks);
}

void arch_apic_timerinit() {
	isr_t *isr = interrupt_allocate(timerisr, arch_apic_eoi, IPL_TIMER);
	__assert(isr);
	int vec = isr->id & 0xff;

	writelapic(APIC_TIMER_DIVIDE, 11);

	writelapic(APIC_TIMER_INITIAL_COUNT, 0xffffffff);

	timekeeper_wait_us(50000);

	time_t ticksperus = (0xffffffff - readlapic(APIC_TIMER_COUNT)) / 50000;

	writelapic(APIC_TIMER_INITIAL_COUNT, 0);

	printf("cpu%lu: local apic timer calibrated at %lu ticks per us. ISR vector %lu\n", current_cpu()->internal_id, ticksperus, vec);

	writelapic(APIC_LVT_TIMER, vec);

	current_cpu()->timer = timer_new(ticksperus, armtimer, stoptimer, time_passed, 0xffffffff);
	__assert(current_cpu()->timer);
}

INIT_ROUTINE_DEFINE(arch_timer, INIT_ROUTINE_FLAGS_NONE, arch_apic_timerinit, timekeeper_early);

void arch_apic_sendipi(uint8_t cpu, uint8_t vec, uint8_t dest, uint8_t mode, uint8_t level) {
	bool status = interrupt_set(false);
	while (readlapic(APIC_REG_ICR_LO) & APIC_REG_ICR_LO_STATUS) CPU_PAUSE();
	writelapic(APIC_REG_ICR_HI, (uint32_t)cpu << 24);
	writelapic(APIC_REG_ICR_LO, vec | (level << 8) | (mode << 11) | (dest << 18) | (1 << 14));
	interrupt_set(status);
}

void arch_apic_init() {
	uacpi_table tbl;

	uacpi_status ret = uacpi_table_find_by_signature("APIC", &tbl);
	__assert(ret == UACPI_STATUS_OK);

	madt = tbl.ptr;
	list_start = (void *)((uintptr_t)madt + sizeof(struct acpi_madt));
	list_end   = (void *)((uintptr_t)madt + madt->hdr.length);

	override_count = getcount(ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE);
	io_count = getcount(ACPI_MADT_ENTRY_TYPE_IOAPIC);
	lapic_count = getcount(ACPI_MADT_ENTRY_TYPE_LAPIC);
	lapic_nmi_count = getcount(ACPI_MADT_ENTRY_TYPE_LAPIC_NMI);

	// map LAPIC to virtual memory

	void *paddr = (void *) rdmsr(MSR_IA32APICBASE);

	lapic_address = vmm_map(NULL, PAGE_SIZE, VMM_FLAGS_PHYSICAL, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, paddr);
	__assert(lapic_address);

	// map I/O apics to memory

	ioapics = alloc(sizeof(ioapic_desc_t) * io_count);
	__assert(ioapics);

	for (int i = 0; i < io_count; ++i) {
		struct acpi_madt_ioapic *entry = getentry(ACPI_MADT_ENTRY_TYPE_IOAPIC, i);

		__assert((entry->address % PAGE_SIZE) == 0);
		ioapics[i].addr = vmm_map(NULL, PAGE_SIZE, VMM_FLAGS_PHYSICAL, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, (void *)(uint64_t)entry->address);
		__assert(ioapics[i].addr);
		ioapics[i].base = entry->gsi_base;
		ioapics[i].top = ioapics[i].base + ((readioapic(ioapics[i].addr, IOAPIC_REG_ENTRY_COUNT) >> 16) & 0xff) + 1;
		printf("ioapic%lu: addr %p base %lu top %lu\n", i, entry->address, entry->gsi_base, ioapics[i].top);
		for (int j = ioapics[i].base; j < ioapics[i].top; ++j)
			writeiored(ioapics[i].addr, j - ioapics[i].base, 0xfe, 0, 0, 0, 0, 1, 0);

	}

	// FIXME limine does this already but just in case the PIC should be masked
}

INIT_ROUTINE_DEFINE(arch_irq, INIT_ROUTINE_FLAGS_NONE, arch_apic_init, acpi_early);
