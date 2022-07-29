#include <arch/isr.h>
#include <arch/panic.h>
#include <kernel/vmm.h>

void isr_general(arch_regs *reg){
	_panic("Unhandled interrupt", 0);
	asm("hlt");
}

void isr_except(arch_regs *reg){
	_panic("CPU Exception", reg);
	asm("hlt;");
}

void isr_pagefault(arch_regs *reg){
	
	if(!vmm_dealwithrequest(reg->cr2))
		_panic("Page fault!\n", 0);
	
}
