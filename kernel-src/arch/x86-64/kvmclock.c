#include <kernel/timekeeper.h>
#include <kernel/alloc.h>
#include <arch/msr.h>
#include <cpuid.h>
#include <logging.h>
#include <arch/cpuid.h>
#include <kernel/pmm.h>
#include <arch/tsc.h>
#include <kernel/cmdline.h>

#define KVM_TIMER_SYSTEM_TIME_ALIGNMENT 4
#define KVM_TIMER_SYSTEM_TIME_ENABLE 1
#define KVM_TIMER_SYSTEM_TIME_VERSION_IN_PROGRESS 1

typedef struct {
	uint32_t version;
	uint32_t padding0;
	uint64_t tsc;
	uint64_t time;
	uint32_t tsc_mul;
	int8_t tsc_shift;
	uint8_t flags;
	uint8_t padding1[2];
} __attribute__((packed)) kvm_timer_info_t;

static bool kvm_timer_probe(void) {
	if (cmdline_get("nokvmclock"))
		return false;

	// figure out if we are running in kvm
	uint32_t base = cpuid_find_hypervisor_base_kvm();
	if (base == 0)
		return false;

	cpuid_results_t cpuid_results;
	// and if the clock is enabled
	cpuid(base + 1, &cpuid_results);
	if (cpuid_results.eax & (1 << 3))
		return true;

	return false;
}

static timekeeper_source_info_t *kvm_timer_init(void) {
	__assert(kvm_timer_probe());

	timekeeper_source_info_t *timekeeper_source_info = alloc(sizeof(timekeeper_source_info_t));
	__assert(timekeeper_source_info);

	timekeeper_source_info->ticks_per_us = 1000; // each tick in the kvm timer system_time field is 1 ns
	void *virtual_address = alloc(sizeof(kvm_timer_info_t));
	__assert(virtual_address);
	__assert(((uintptr_t)virtual_address % KVM_TIMER_SYSTEM_TIME_ALIGNMENT) == 0);

	int page_offset = (uintptr_t)virtual_address % PAGE_SIZE;
	void *virtual_page = (void *)((uintptr_t)virtual_address - page_offset);
	void *physical_page = vmm_getphysical(virtual_page, false);
	__assert(physical_page);

	timekeeper_source_info->private = (void *)((uintptr_t)physical_page + page_offset);

	wrmsr(MSR_KVM_SYSTEM_TIME_NEW, (uint64_t)timekeeper_source_info->private | KVM_TIMER_SYSTEM_TIME_ENABLE);

	return timekeeper_source_info;
}

// ran in at least IPL_DPC
static time_t kvm_timer_ticks(timekeeper_source_info_t *timekeeper_source_info) {
	volatile kvm_timer_info_t *kvm_timer_info = MAKE_HHDM(timekeeper_source_info->private);

	// if version is odd, an update is in progress.
	// we will wait until one isnt
	uint32_t version_old;
	uint32_t version_new;
	uint64_t ticks = 0;
	do {
		do {
			version_old = kvm_timer_info->version;
		} while (version_old & KVM_TIMER_SYSTEM_TIME_VERSION_IN_PROGRESS);

		uint64_t tsc = rdtsc_serialized();
		ticks = tsc - kvm_timer_info->tsc;
		ticks = (kvm_timer_info->tsc_shift >= 0) ? (ticks << kvm_timer_info->tsc_shift) : (ticks >> -kvm_timer_info->tsc_shift);

		// we need to pretend ticks is a *128* bit variable!
		// this will be done in just an inline assembly stub...

		// mulq will leave the 128 bit results in rdx:rax
		// shrd will do the 32 bit shift required
		// we will stay with the low 64 bits and leave the high 32 bits, as they are not nescessary
		// (the 64 bit nanosecond counter will overflow in centuries...)
		asm volatile ("mulq %%rdx; shrd $32, %%rdx, %%rax" : "=a"(ticks) : "a"(ticks), "d"(kvm_timer_info->tsc_mul));

		ticks = ticks + kvm_timer_info->time;

		version_new = kvm_timer_info->version;
	} while (version_old != version_new || (version_new & KVM_TIMER_SYSTEM_TIME_VERSION_IN_PROGRESS));

	return ticks;
}

TIMEKEEPER_SOURCE(kvmtimer_source,
	.name = "KVM clock",
	.priority = 1000,
	.probe = kvm_timer_probe,
	.init = kvm_timer_init,
	.ticks = kvm_timer_ticks,
	.flags = TIMEKEEPER_SOURCE_FLAGS_EARLY
);
