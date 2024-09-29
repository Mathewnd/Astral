#ifndef _CPUID_H
#define _CPUID_H

typedef struct {
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
} cpuid_results_t;

#define CPUID_HYPERVISOR_KVM_EBX 0x4b4d564b
#define CPUID_HYPERVISOR_KVM_ECX 0x564b4d56
#define CPUID_HYPERVISOR_KVM_EDX 0x4d

#define CPUID_HYPERVISOR_IS_KVM(ebx, ecx, edx) ((ebx) == CPUID_HYPERVISOR_KVM && (ecx) == CPUID_HYPERVISOR_KVM_ECX && (edx) == CPUID_HYPERVISOR_KVM_EDX)

#define CPUID_VENDOR_AMD "AuthenticAMD"
#define CPUID_VENDOR_INTEL "GenuineIntel"

#define CPUID_LEAF_0x80000001_EDX_SYSCALL (1 << 11)
#define CPUID_LEAF_1_EDX_HTT (1 << 28)

static inline void cpuid_with_ecx(uint32_t leaf, uint32_t ecx, cpuid_results_t *cpuid_results) {
	asm volatile ("cpuid"
			:
			"=a"(cpuid_results->eax),
			"=b"(cpuid_results->ebx),
			"=c"(cpuid_results->ecx),
			"=d"(cpuid_results->edx)
			:
			"a"(leaf),
			"c"(ecx));
}

static inline void cpuid(uint32_t leaf, cpuid_results_t *cpuid_results) {
	return cpuid_with_ecx(leaf, 0, cpuid_results);
}

static inline uint32_t cpuid_find_hypervisor_base(uint32_t ebx, uint32_t ecx, uint32_t edx) {
	uint32_t return_base = 0;
	cpuid_results_t cpuid_results = {0};

	for (uint32_t base = 0x40000000; base < 0x40001000; base += 0x100) {
		cpuid(base, &cpuid_results);
		if (cpuid_results.ebx == ebx && cpuid_results.ecx == ecx && cpuid_results.edx == edx) {
			return_base = base;
			break;
		}
	}

	return return_base;
}

static inline uint32_t cpuid_find_hypervisor_base_kvm(void) {
	return cpuid_find_hypervisor_base(CPUID_HYPERVISOR_KVM_EBX, CPUID_HYPERVISOR_KVM_ECX, CPUID_HYPERVISOR_KVM_EDX);
}

static inline uint32_t cpuid_base_max_leaf(void) {
	cpuid_results_t cpuid_results;
	cpuid(0, &cpuid_results);
	return cpuid_results.eax;
}

static inline uint32_t cpuid_hypervisor_max_leaf(uint32_t base) {
	cpuid_results_t cpuid_results;
	cpuid(base, &cpuid_results);
	return cpuid_results.eax;
}

static inline uint32_t cpuid_extended_max_leaf(void) {
	cpuid_results_t cpuid_results;
	cpuid(0x80000000, &cpuid_results);
	return cpuid_results.eax;
}

static inline bool cpuid_leaf_0xb_available(void) {
	if (cpuid_base_max_leaf() < 0xb)
		return false;

	cpuid_results_t cpuid_results;
	cpuid(0xb, &cpuid_results);
	return cpuid_results.ebx != 0;
}

static inline bool cpuid_leaf_0x80000008_available(void) {
	return cpuid_extended_max_leaf() >= 0x80000008;
}

static inline bool cpuid_leaf_0x4_available(void) {
	if (cpuid_base_max_leaf() < 0x4)
		return false;

	unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
	__get_cpuid_count(0x4, 0, &eax, &ebx, &ecx, &edx);
	return eax != 0;
}

#endif
