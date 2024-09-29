#include <kernel/timekeeper.h>
#include <arch/tsc.h>
#include <arch/cpuid.h>
#include <logging.h>
#include <kernel/alloc.h>

static bool tsc_probe(void) {
	cpuid_results_t cpuid_results;

	uint32_t max_extended_leaf = cpuid_extended_max_leaf();
	if (max_extended_leaf < 0x80000007)
		return false;

	cpuid(0x80000007, &cpuid_results);
	return cpuid_results.edx & (1 << 8);
}

static timekeeper_source_info_t *tsc_init(void) {
	__assert(tsc_probe());

	timekeeper_source_info_t *timekeeper_source_info = alloc(sizeof(timekeeper_source_info_t));
	__assert(timekeeper_source_info);

	timekeeper_source_info->private = current_cpu();

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
		timekeeper_source_info->ticks_per_us = tsc_frequency / 1000000;
	}

	calibrate:
	if (need_calibration) {
		for (int i = 0; i < 10; ++i) {
			long old_ipl = interrupt_raiseipl(IPL_TIMER - 1);
			time_t ticks_start = rdtsc_serialized();

			timekeeper_wait_us(50000);
			time_t ticks_end = rdtsc_serialized();

			interrupt_loweripl(old_ipl);

			timekeeper_source_info->ticks_per_us = (ticks_end - ticks_start) / 50000;
		}
	}

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
