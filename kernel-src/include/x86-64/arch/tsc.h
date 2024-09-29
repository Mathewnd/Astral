#ifndef _TSC_H
#define _TSC_H

static inline uint64_t rdtsc_serialized(void) {
	uint32_t high;
	uint32_t low;
	asm volatile("cpuid; rdtsc;" : "=a"(low), "=d"(high) : : "rbx", "rcx");
	return ((uint64_t)high << 32) | low;
}

#endif
