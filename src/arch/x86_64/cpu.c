#include <arch/cpu.h>
#include <arch/msr.h>
#include <cpuid.h>
#include <arch/cls.h>
#include <stdio.h>
#include <kernel/pmm.h>
#include <arch/idt.h>

// initializes things like 
// syscall, sse, etc
// for the current cpu

extern void asm_syscall_entry();

void cpu_state_init(){
	
	// syscall

	// does it exist
	
	int eax,ebx,ecx,edx;

	__get_cpuid(0x80000001, &eax, &ebx, &ecx, &edx);

	if(edx & (1 << 11)){ // check for the syscall bit
		uint64_t efer = rdmsr(MSR_EFER);
		efer |= 1; // set syscall enable bit
		wrmsr(MSR_EFER, efer);
		
		uint64_t star = 0;
		star |= (uint64_t)0x28 << 48;
		star |= (uint64_t)0x28 << 32;
		
		wrmsr(MSR_STAR, star);
		wrmsr(MSR_LSTAR, asm_syscall_entry);
		wrmsr(MSR_CSTAR, 0); // no compat mode syscall handler
		wrmsr(MSR_FMASK, 0x200); // disable interrupts on syscall
	}
	// undefined opcode exception will handle system calls then
	else printf("CPU%lu: syscall instruction not supported\n", arch_getcls()->lapicid);

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

	// set up interrupt stacks
	
	// timer stack is 1

	 arch_getcls()->ist.ist1 = pmm_hhdmalloc(10) + PAGE_SIZE*10;


}
