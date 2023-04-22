#include <arch/acpi.h>
#include <time.h>
#include <logging.h>
#include <arch/hpet.h>
#include <kernel/vmm.h>

#define HPET_REG_CAPS 0
#define HPET_REG_CONFIG 2
#define HPET_REG_INTSTATUS 4
#define HPET_REG_COUNTER 0x1e

#define CAP_FSPERTICK(x) (((x) >> 32) & 0xffffffff)

// XXX there might be several hpets in a system.
static volatile uint64_t *hpet;
static time_t ticksperus;

static uint64_t read64(int reg) {
	return hpet[reg];
}

static void write64(int reg, uint64_t v) {
	hpet[reg] = v;
}

void arch_hpet_waitticks(time_t ticks) {
	uint64_t target = read64(HPET_REG_COUNTER) + ticks;
	while (target > read64(HPET_REG_COUNTER)) asm volatile("pause");
}

void arch_hpet_waitus(time_t us) {
	arch_hpet_waitticks(us * ticksperus);
}

bool arch_hpet_exists() {
	return arch_acpi_findtable("HPET", 0) != NULL;
}

void arch_hpet_init() {
	__assert(arch_hpet_exists());
	hpet_t *table = arch_acpi_findtable("HPET", 0);
	printf("hpet%lu: %lu bits %lu comparators\n", table->hpetnum, 32 + table->countersize * 32, table->comparatorcount);
	__assert(table->countersize); // XXX not rely on hpet being 64 bits
	__assert(table->addrid == 0);
	__assert((table->addr & PAGE_SIZE) == 0);
	hpet = vmm_map(NULL, PAGE_SIZE, VMM_FLAGS_PHYSICAL, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, (void *)table->addr);
	__assert(hpet);
	ticksperus = 1000000000 / CAP_FSPERTICK(read64(HPET_REG_CAPS));
	printf("hpet%lu: %lu ticks per us (%lu fs per tick)\n", table->hpetnum, ticksperus, CAP_FSPERTICK(read64(HPET_REG_CAPS)));
	__assert(ticksperus);

	write64(HPET_REG_CONFIG, 0);
	write64(HPET_REG_COUNTER, 0);
	write64(HPET_REG_CONFIG, 1);
}
