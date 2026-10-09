#include <kernel/init.h>
#include <string.h>
#include <arch/cpu.h>
#include <arch/cpuid.h>
#include <kernel/prof.h>
#include <logging.h>

#ifdef ENABLE_PROFILING

#define INTEL_TSC_INDEX 2
#define INTEL_TSC_INDEX_OFFSET (1lu << 34)
#define INTEL_TSC_FIXED_CTR_CTRL_OFFSET 8
#define INTEL_TSC_FIXED_CTR_CTRL_ENABLE (1 << (11))
#define INTEL_PERF_FIXED_CTR_CTRL_USER 1
#define INTEL_PERF_FIXED_CTR_CTRL_SUPERVISOR 2

#define TICK_COUNT 3200000

static int intel_get_max_size(void) {
	cpuid_results_t cpuid_results;
	cpuid(0xa, &cpuid_results);
	return 1 << ((cpuid_results.eax >> 16) & 0xff);
}

static int intel_get_version(void) {
	cpuid_results_t cpuid_results;
	cpuid(0xa, &cpuid_results);
	return cpuid_results.eax & 0xff;
}

static bool intel_check_tsc_fixed(void) {
	cpuid_results_t cpuid_results;
	cpuid(0xa, &cpuid_results);

	int fixed_count = cpuid_results.edx & 0x1f;
	return fixed_count > INTEL_TSC_INDEX;
}

static void reset_intel_pmc(void) {
	// disable PMCs
	wrmsr(MSR_PERF_FIXED_CTR_CTRL, 0);

	// clear overflow
	wrmsr(MSR_PERF_GLOBAL_OVF_CTRL, INTEL_TSC_INDEX_OFFSET);

	// program PMC
	wrmsr(MSR_PERF_FIXED_CTR2, intel_get_max_size() - TICK_COUNT);

	// enable the PMC
	wrmsr(MSR_PERF_FIXED_CTR_CTRL,
		((INTEL_PERF_FIXED_CTR_CTRL_USER | INTEL_PERF_FIXED_CTR_CTRL_SUPERVISOR) << INTEL_TSC_FIXED_CTR_CTRL_OFFSET) |
		INTEL_TSC_FIXED_CTR_CTRL_ENABLE);
}

static void init_intel_pmc(void) {
	// check for version 2 PMC
	if (intel_get_version() < 2) {
		__assert(!"Profiling is not supported on Intel CPUs without at least a version 2 PMC. Recompile without profiling.");
	}

	// check for support for the tsc fixed counter
	__assert(intel_check_tsc_fixed());

	// enable fixed PMC
	wrmsr(MSR_PERF_GLOBAL_CTRL, rdmsr(MSR_PERF_GLOBAL_CTRL) | INTEL_TSC_INDEX_OFFSET);

	reset_intel_pmc();
	arch_apic_init_perf();
}

static bool backtrace_fn(void *context, void *ip) {
	uint8_t *data = context;

	if (data[0] == 255)
		return false;

	uintptr_t *trace = (uintptr_t *)&data[1];
	trace[data[0]] = (uintptr_t)ip;
	data[0] += 1;

	return true;
}

static bool intel_irq(context_t *context) {
	if ((rdmsr(MSR_PERF_GLOBAL_STATUS) & INTEL_TSC_INDEX_OFFSET) == 0)
		return false;

	uint8_t data[1 + sizeof(uintptr_t) * 255];
	data[0] = 0;
	arch_get_backtrace(context, backtrace_fn, data);
	profiling_insert(data[0], data);

	arch_apic_init_perf(); // SDM LVT figure says a PMC irq masks itself, so unmask it
	reset_intel_pmc();
	return true;
}

static bool amd_irq(context_t *context) {
	__assert(!"Unimplemented");
}


static void init_amd_pmc(void) {
	__assert(!"Unimplemented");
}

bool arch_profiling_irq(context_t *context) {
	if (!strcmp(current_cpu()->vendor, "AuthenticAMD"))
		return amd_irq(context);
	else if (!strcmp(current_cpu()->vendor, "GenuineIntel"))
		return intel_irq(context);

	__assert(!"unreachable");
}


void arch_profiling_init(void) {
	arch_apic_init_perf();

	if (!strcmp(current_cpu()->vendor, "AuthenticAMD"))
		init_amd_pmc();
	else if (!strcmp(current_cpu()->vendor, "GenuineIntel"))
		init_intel_pmc();
	else
		printf("arch_prof: no PMC for this vendor\n");
}

INIT_ROUTINE_DEFINE(arch_profiling, INIT_ROUTINE_FLAGS_NONE, arch_profiling_init, profiling, cpu);

#endif
