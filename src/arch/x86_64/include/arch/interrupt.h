#ifndef _ARCH_INTERRUPT_H_INCLUDE
#define _ARCH_INTERRUPT_H_INCLUDE

static inline void arch_interrupt_disable(){
	asm("cli");
}

static inline void arch_interrupt_enable(){
	asm("sti");
}

#endif
