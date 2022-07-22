#include <arch/isr.h>
#include <arch/panic.h>

void isr_general(arch_regsnoerror *reg){
	_panic("Unhandled interrupt", 0);
	asm("hlt");
}

void isr_except(arch_regserror *reg){
	_panic("CPU Exception", reg);
	asm("hlt;");
}

void isr_pagefault(void*){
	
}
