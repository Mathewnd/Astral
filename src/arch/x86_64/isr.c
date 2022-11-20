#include <arch/isr.h>
#include <arch/panic.h>
#include <kernel/vmm.h>
#include <arch/apic.h>
#include <kernel/timer.h>

void isr_simd(arch_regs *reg){
	
	uint64_t mxcsr;

	asm volatile("stmxcsr (%%rax)" : : "a"(&mxcsr));

	printf("SIMD: %p\n", mxcsr);
	
	_panic("SIMD exception", reg);
}

void isr_general(arch_regs *reg, int n){
	printf("interrupt %d", n);
	_panic("Unhandled interrupt", reg);
}

void isr_except(arch_regs *reg, int n){
	printf("exception %d", n);
	_panic("CPU Exception", reg);
}

void isr_pagefault(arch_regs *reg){
	
	if(!vmm_dealwithrequest(reg->cr2, reg->error, reg->cs == 0x3b))
		_panic("Page fault!", reg);
	
}

void isr_lapicnmi(){
	_panic("Local apic NMI", 0);
}

void isr_mmuinval(){
	arch_mmu_invalidateipi();
	apic_eoi();
}

void isr_ps2kbd(arch_regs* reg){
	ps2kbd_irq();
	apic_eoi();
}

void isr_timer(arch_regs* reg){
	timer_irq(reg);
	apic_eoi();
}
