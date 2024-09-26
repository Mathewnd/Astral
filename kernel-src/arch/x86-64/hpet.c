#include <arch/acpi.h>
#include <time.h>
#include <logging.h>
#include <arch/hpet.h>
#include <kernel/vmm.h>
#include <kernel/interrupt.h>

#include <uacpi/acpi.h>
#include <uacpi/tables.h>

#define HPET_REG_CAPS 0
#define HPET_REG_CONFIG 2
#define HPET_REG_INTSTATUS 4
#define HPET_REG_COUNTER 0x1e

#define HPET_TIMER_CONFIG(t) (0x20 + 0x4 * (t))
#define HPET_TIMER_COMPARATOR(t) (0x21 + 0x4 * (t))
#define HPET_TIMER_FSBROUTE(t) (0x22 + 0x4 * (t))

#define HPET_TIMER_CONFIG_IRQTYPE (1l << 1) // 0 for edge trigger 1 for level trigger
#define HPET_TIMER_CONFIG_IRQENABLE (1l << 2)
#define HPET_TIMER_CONFIG_PERIODIC (1l << 3)
#define HPET_TIMER_CONFIG_FORCE32BIT (1l << 8)
#define HPET_TIMER_CONFIG_FSBENABLE (1l << 14)
#define HPET_TIMER_CONFIG_FSBCAPABLE (1l << 15)

#define HPET_CAP_LEGACYCAPABLE (1 << 15)
#define HPET_CAP_FSPERTICK(x) (((x) >> 32) & 0xffffffff)

static volatile uint64_t *hpet;
static time_t ticksperus;
static uint64_t tickspassed;

// in order of preference
#define TYPE_64BIT 0
#define TYPE_LEGACY 1
static int type;

static uint64_t read64(int reg) {
	return hpet[reg];
}

static void write64(int reg, uint64_t v) {
	hpet[reg] = v;
}

time_t arch_hpet_ticks() {
	if (type == TYPE_64BIT) {
		return read64(HPET_REG_COUNTER);
	} else {
		uint64_t ticks;
		do {
			ticks = __atomic_load_n(&tickspassed, __ATOMIC_SEQ_CST) + (read64(HPET_REG_COUNTER) & 0xffffffff);
		} while (ticks < __atomic_load_n(&tickspassed, __ATOMIC_SEQ_CST));
		return ticks;
	}
}

void arch_hpet_waitticks(time_t ticks) {
	uint64_t target = arch_hpet_ticks() + ticks;
	while (target > arch_hpet_ticks()) asm volatile("pause");
}

void arch_hpet_waitus(time_t us) {
	arch_hpet_waitticks(us * ticksperus);
}

static void counterirq(isr_t *isr, context_t *context) {
	write64(HPET_REG_CONFIG, 0);

	__atomic_add_fetch(&tickspassed, read64(HPET_REG_COUNTER), __ATOMIC_SEQ_CST);
	write64(HPET_REG_COUNTER, 0);

	write64(HPET_REG_CONFIG, 1 + ((type == TYPE_LEGACY) ? 2 : 0));
}

bool arch_hpet_exists() {
	// This is assumed by arch_hpet_init below
	return true;
}

time_t arch_hpet_init() {
	uacpi_table tbl;
	struct acpi_hpet *table;

	if (uacpi_table_find_by_signature("HPET", &tbl) != UACPI_STATUS_OK) {
		printf("hpet: no timers\n");
		return 0;
	}

	table = tbl.ptr;

	printf("hpet%lu: %lu bits %lu comparators\n", table->number,
		(table->block_id & ACPI_HPET_COUNT_SIZE_CAP) ? 64 : 32,
		(table->block_id >> ACPI_HPET_NUMBER_OF_COMPARATORS_SHIFT) & ACPI_HPET_NUMBER_OF_COMPARATORS_MASK);
	__assert(table->address.address_space_id == ACPI_AS_ID_SYS_MEM);

	uint64_t page_offset = table->address.address % PAGE_SIZE;
	hpet = vmm_map(NULL, (PAGE_SIZE + page_offset), VMM_FLAGS_PHYSICAL, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, (void *)(table->address.address - page_offset));
	__assert(hpet);
	hpet = (void *)((uintptr_t)hpet + page_offset);

	uint64_t capabilities = read64(HPET_REG_CAPS);

	ticksperus = 1000000000 / HPET_CAP_FSPERTICK(capabilities);
	printf("hpet%lu: %lu ticks per us (%lu fs per tick)\n", table->number, ticksperus, HPET_CAP_FSPERTICK(capabilities));
	__assert(ticksperus);

	write64(HPET_REG_CONFIG, 0);
	write64(HPET_REG_COUNTER, 0);

	if (!(table->block_id & ACPI_HPET_COUNT_SIZE_CAP)) {
		int selectedtimer = 2;
		uint64_t timerconfig = read64(HPET_TIMER_CONFIG(selectedtimer));
		// a 32 bit main counter will require more handling on our side.
		isr_t *isr = interrupt_allocate(counterirq, arch_apic_eoi, IPL_TIMER);
		__assert(isr);

		// first check how interrupts will be handled and initialise them
		// TODO FSB mapping
		// timer 0 will be used with the timer irq replacing the PIT's as irq0.
		if (capabilities & HPET_CAP_LEGACYCAPABLE) {
			type = TYPE_LEGACY;

			arch_ioapic_setirq(0, isr->id & 0xff, current_cpu_id(), false);

			selectedtimer = 0;
			timerconfig = read64(HPET_TIMER_CONFIG(selectedtimer));
			timerconfig &= ~(HPET_TIMER_CONFIG_FSBENABLE);
		} else {
			__assert(!"no supported ways of handling irq for 32 bit hpet");
		}

		// finish configuration and write it
		timerconfig &= ~(HPET_TIMER_CONFIG_IRQTYPE | HPET_TIMER_CONFIG_PERIODIC);
		timerconfig |= HPET_TIMER_CONFIG_IRQENABLE | HPET_TIMER_CONFIG_FORCE32BIT;

		write64(HPET_TIMER_CONFIG(selectedtimer), timerconfig);
		write64(HPET_TIMER_COMPARATOR(selectedtimer), 0x80000000);
		printf("hpet%d: timer %d on isr %d using %s\n", table->number, selectedtimer, isr->id & 0xff, "legacy replacement");
	} else {
		type = TYPE_64BIT;
	}

	write64(HPET_REG_CONFIG, 1 + ((type == TYPE_LEGACY) ? 2 : 0));

	uacpi_table_unref(&tbl);
	return ticksperus;
}
