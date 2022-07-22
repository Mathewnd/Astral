#include <arch/idt.h>
#include <arch/isr.h>
#include <stdint.h>
#include <stddef.h>

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
	
	for(size_t i = 0; i < 32; ++i){
		idt_setentry(&idt[i], asmisr_except, 0x28, FLAGS_PRESENT | FLAGS_TYPE_TRAP, 0);
	}
	
	idt_reload();

	asm("int $0x0");

}
