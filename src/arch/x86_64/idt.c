#include <arch/idt.h>
#include <arch/isr.h>
#include <stdint.h>
#include <stddef.h>

// the IDT should look like this:
// 
// 0-0x19 -> exceptions
// 0x20 -> panic ipi
// 0x21 -> lapic nmi 
// 0x22 -> mmu invalidate page ipi
// ...
// 0x40 -> ps2 keyboard isr
// ...
// 0x80 -> timer
// ...

idtentry_t idt[256];

void idt_setentry(idtentry_t *e, uint64_t offset, uint16_t segment, uint16_t flags, uint8_t ist){
	e->offset1 = offset & 0xFFFF;
	e->offset2 = (offset >> 16) & 0xFFFF;
	e->offset3 = (offset >> 32) & 0xFFFFFFFF;
	e->segment = segment;
	e->flags   = flags | ist;
	e->reserved = 0;
}

static void idt_reload(){
	
	idtr_t idtr;
	idtr.offset = (uint64_t)&idt;
	idtr.size = sizeof(idt)-1;

	asm("lidt (%%rax)": : "a"(&idtr) : "memory");

}

void idt_bspinit(){

	for(size_t i = 0; i < 256; ++i){
		idt_setentry(&idt[i], asmisr_general, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);
	}

	// set error code pushers as exceptions
	
	idt_setentry(&idt[VECTOR_DOUBLEFAULT], asmisr_except, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);
	idt_setentry(&idt[VECTOR_GPF], asmisr_except, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);
	idt_setentry(&idt[VECTOR_INVALIDTSS], asmisr_except, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);
	idt_setentry(&idt[VECTOR_SEGMENTNOTPRESENT], asmisr_except, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);
	idt_setentry(&idt[VECTOR_STACK], asmisr_except, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);
	

	
	idt_setentry(&idt[VECTOR_SIMD], asmisr_simd, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);
	
	idt_setentry(&idt[VECTOR_PF], asmisr_pagefault, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);
	
	idt_setentry(&idt[VECTOR_GPF], asmisr_gpf, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);
	
	idt_setentry(&idt[VECTOR_DEVUNAVAILABLE], asmisr_nm, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);
	
	idt_setentry(&idt[VECTOR_PANIC], asmisr_panic, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);

	idt_setentry(&idt[VECTOR_MMUINVAL], asmisr_mmuinval, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);

	idt_setentry(&idt[VECTOR_LAPICNMI], asmisr_lapicnmi, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);

	idt_setentry(&idt[VECTOR_TIMER], asmisr_timer, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, TIMER_IST);

	idt_setentry(&idt[VECTOR_PS2MOUSE], asmisr_ps2mouse, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);
	
	idt_setentry(&idt[VECTOR_PS2KBD], asmisr_ps2kbd, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);
	
	idt_setentry(&idt[VECTOR_NVME], asmisr_nvme, 0x28, FLAGS_PRESENT | FLAGS_TYPE_INTERRUPT, 0);
	
	idt_reload();

}

void idt_init(){
	
	idt_reload();

}
