#include <arch/cpu.h>
#include <arch/apic.h>
#include <cpuid.h>
#include <logging.h>

#define CPUID_SYSCALL (1 << 11)
#define EFER_SYSCALLENABLE 1

void arch_syscall_entry();

void cpu_initstate() {
	arch_apic_initap();

	// syscall instruction
	unsigned int eax = 0,ebx = 0,ecx = 0,edx = 0;
	__get_cpuid(0x80000001, &eax, &ebx, &ecx, &edx);

	// TODO in the future, have an alternative way of handling syscalls
	__assert(edx & CPUID_SYSCALL);
	uint64_t efer = rdmsr(MSR_EFER);
	efer |= EFER_SYSCALLENABLE;
	wrmsr(MSR_EFER, efer);

	uint64_t star = 0;
	star |= (uint64_t)0x13 << 48;
	star |= (uint64_t)0x08 << 32;

	wrmsr(MSR_STAR, star);
	wrmsr(MSR_LSTAR, (uint64_t)arch_syscall_entry);
	wrmsr(MSR_CSTAR, 0); // no compatibility mode syscall handler
	wrmsr(MSR_FMASK, 0x200); // disable interrupts on syscall

	// enable SSE
	asm volatile(
		"mov %%cr0, %%rax;"
		"and $0xFFFB, %%ax;"
		"or  $2, %%eax;"
		"mov %%rax, %%cr0;"
		"mov %%cr4, %%rax;"
		"or  $0b11000000000, %%rax;"
		"mov %%rax, %%cr4;"
		: : : "rax");
}
