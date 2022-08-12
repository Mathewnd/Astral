#include <arch/isr.h>
#include <arch/panic.h>
#include <kernel/vmm.h>
#include <arch/apic.h>

void isr_general(arch_regs *reg){
	_panic("Unhandled interrupt", reg);
	asm("hlt");
}

void isr_except(arch_regs *reg){
	_panic("CPU Exception", reg);
	asm("hlt;");
}

void isr_pagefault(arch_regs *reg){
	
	if(!vmm_dealwithrequest(reg->cr2))
		_panic("Page fault!", reg);
	
}

void isr_lapicnmi(){
	_panic("Local apic NMI", 0);
}

void isr_mmuinval(){
	arch_mmu_invalidateipi();
	apic_eoi();
}

void isr_timer(arch_regs* reg){
	apic_eoi();
}
