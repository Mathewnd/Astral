#include <arch/isr.h>
#include <arch/panic.h>
#include <kernel/vmm.h>
#include <arch/apic.h>
#include <kernel/timer.h>
#include <arch/cls.h>
#include <stdio.h>
#include <errno.h>

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

void isr_gpf(arch_regs* reg){
	
	arch_getcls()->thread->umemoperror = EFAULT;

	if(arch_getcls()->thread->umemopfailaddr == NULL)
		_panic("General protection fault!", reg);
	else
		reg->rip = (uint64_t)arch_getcls()->thread->umemopfailaddr;
	


}

void isr_pagefault(arch_regs *reg){
	int error = vmm_dealwithrequest(reg->cr2, reg->error, reg->cs == 0x3b);

	if(error){
		if(arch_getcls()->thread->umemopfailaddr == NULL){
			_panic("Page fault!", reg);
		}
		else{
			reg->rip = (uint64_t)arch_getcls()->thread->umemopfailaddr;
			arch_getcls()->thread->umemoperror = error;
			}
	}
	
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

void isr_ps2mouse(arch_regs* reg){
	ps2mouse_irq();
	apic_eoi();
}

void isr_timer(arch_regs* reg){
	timer_irq(reg);
	apic_eoi();
}
