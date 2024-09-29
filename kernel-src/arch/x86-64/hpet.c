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

// in order of preference
#define TYPE_64BIT 0
#define TYPE_LEGACY 1


static MUTEX_DEFINE(init_mutex);
static struct {
	volatile uint64_t *hpet;
	uint64_t tickspassed;
	int type;
} hpet_private;

static timekeeper_source_info_t timekeeper_source_info = {
	.private = &hpet_private
};

static uint64_t read64(int reg) {
	return hpet_private.hpet[reg];
}

static void write64(int reg, uint64_t v) {
	hpet_private.hpet[reg] = v;
}

static void counterirq(isr_t *isr, context_t *context) {
	write64(HPET_REG_CONFIG, 0);

	__atomic_add_fetch(&hpet_private.tickspassed, read64(HPET_REG_COUNTER), __ATOMIC_SEQ_CST);
	write64(HPET_REG_COUNTER, 0);

	write64(HPET_REG_CONFIG, 1 + ((hpet_private.type == TYPE_LEGACY) ? 2 : 0));
}

static bool hpet_probe(void) {
	bool usable = false;
	struct acpi_hpet *table;
	uacpi_table tbl;

	if (uacpi_table_find_by_signature("HPET", &tbl) != UACPI_STATUS_OK)
		return false;

	table = tbl.ptr;

	if (table->address.address_space_id != ACPI_AS_ID_SYS_MEM)
		goto leave;

	usable = true;
	leave:
	uacpi_table_unref(&tbl);
	return usable;
}

static timekeeper_source_info_t *hpet_init(void) {
	uacpi_table tbl;
	struct acpi_hpet *table;

	MUTEX_ACQUIRE(&init_mutex, false);
	if (timekeeper_source_info.ticks_per_us) {
		// already initialized by someone else
		goto leave;
	}

	__assert(uacpi_table_find_by_signature("HPET", &tbl) == UACPI_STATUS_OK);
	table = tbl.ptr;
	printf("hpet%lu: %lu bits %lu comparators\n", table->number,
		(table->block_id & ACPI_HPET_COUNT_SIZE_CAP) ? 64 : 32,
		(table->block_id >> ACPI_HPET_NUMBER_OF_COMPARATORS_SHIFT) & ACPI_HPET_NUMBER_OF_COMPARATORS_MASK);
	__assert(table->address.address_space_id == ACPI_AS_ID_SYS_MEM);

	uint64_t page_offset = table->address.address % PAGE_SIZE;
	hpet_private.hpet = vmm_map(NULL, (PAGE_SIZE + page_offset), VMM_FLAGS_PHYSICAL, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, (void *)(table->address.address - page_offset));
	__assert(hpet_private.hpet);
	hpet_private.hpet = (void *)((uintptr_t)hpet_private.hpet + page_offset);

	uint64_t capabilities = read64(HPET_REG_CAPS);
	timekeeper_source_info.ticks_per_us = 1000000000 / HPET_CAP_FSPERTICK(capabilities);
	printf("hpet%lu: %lu ticks per us (%lu fs per tick)\n", table->number, timekeeper_source_info.ticks_per_us, HPET_CAP_FSPERTICK(capabilities));
	__assert(timekeeper_source_info.ticks_per_us);

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
			hpet_private.type = TYPE_LEGACY;

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
		hpet_private.type = TYPE_64BIT;
	}

	write64(HPET_REG_CONFIG, 1 + ((hpet_private.type == TYPE_LEGACY) ? 2 : 0));

	uacpi_table_unref(&tbl);

	leave:
	MUTEX_RELEASE(&init_mutex);
	return &timekeeper_source_info;

}

// ran in at least IPL_DPC
time_t hpet_ticks(timekeeper_source_info_t *) {
	if (hpet_private.type == TYPE_64BIT) {
		return read64(HPET_REG_COUNTER);
	} else {
		uint64_t ticks;
		do {
			ticks = __atomic_load_n(&hpet_private.tickspassed, __ATOMIC_SEQ_CST) + (read64(HPET_REG_COUNTER) & 0xffffffff);
		} while (ticks < __atomic_load_n(&hpet_private.tickspassed, __ATOMIC_SEQ_CST));
		return ticks;
	}
}

TIMEKEEPER_SOURCE(
	hpet_source,
	.name = "HPET",
	.priority = 100,
	.probe = hpet_probe,
	.init = hpet_init,
	.ticks = hpet_ticks,
	.flags = TIMEKEEPER_SOURCE_FLAGS_EARLY
);
