#include <kernel/timekeeper.h>
#include <arch/tsc.h>
#include <arch/cpuid.h>
#include <logging.h>
#include <kernel/alloc.h>
#include <arch/hpet.h>
#include <kernel/cmdline.h>

static bool tsc_probe(void) {
	if (cmdline_get("notsc"))
		return false;

	cpuid_results_t cpuid_results;

	uint32_t max_extended_leaf = cpuid_extended_max_leaf();
	if (max_extended_leaf < 0x80000007)
		return false;

	cpuid(0x80000007, &cpuid_results);
	return cpuid_results.edx & (1 << 8);
}

static MUTEX_DEFINE(init_mutex);
static time_t ticks_per_us;

static timekeeper_source_info_t *tsc_init(void) {
	MUTEX_ACQUIRE(&init_mutex, false);
	__assert(tsc_probe());

	timekeeper_source_info_t *timekeeper_source_info = alloc(sizeof(timekeeper_source_info_t));
	__assert(timekeeper_source_info);

	timekeeper_source_info->private = current_cpu();

	if (ticks_per_us)
		goto leave;

	bool need_calibration = false;
	if (cpuid_base_max_leaf() >= 0x15) {
		cpuid_results_t cpuid_results;
		cpuid(0x15, &cpuid_results);

		if (cpuid_results.eax == 0 || cpuid_results.ebx == 0 || cpuid_results.ecx == 0) {
			need_calibration = true;
			goto calibrate;
		}

		uint64_t core_frequency = cpuid_results.ecx;
		uint64_t core_numerator = cpuid_results.ebx;
		uint64_t core_denominator = cpuid_results.eax;

		uint64_t tsc_frequency = core_frequency * core_numerator / core_denominator;
		ticks_per_us = tsc_frequency / 1000000;
	}

	calibrate:
	if (need_calibration) {
		ticks_per_us = arch_hpet_calibrate_tsc(200) / 1000;
		__assert(ticks_per_us);
	}

	leave:
	timekeeper_source_info->ticks_per_us = ticks_per_us;
	MUTEX_RELEASE(&init_mutex);
	return timekeeper_source_info;
}

// ran in at least IPL_DPC
static time_t tsc_ticks(timekeeper_source_info_t *timekeeper_source_info) {
	__assert(timekeeper_source_info->private == current_cpu());

	return rdtsc_serialized();
}

TIMEKEEPER_SOURCE(kvmtimer_source,
	.name = "Invariant TSC",
	.priority = 990,
	.probe = tsc_probe,
	.init = tsc_init,
	.ticks = tsc_ticks,
	.flags = 0
);
