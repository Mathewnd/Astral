#include <arch/isr.h>
#include <arch/panic.h>

void isr_general(void*){
	_panic("Unhandled interrupt");
	asm("hlt");
}

void isr_except(void*){
	_panic("CPU Exception");
	asm("hlt;");
}

void isr_pagefault(void*){

}
