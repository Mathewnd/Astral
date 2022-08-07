#ifndef _ISR_H_INCLUDE
#define _ISR_H_INCLUDE

#include <arch/regs.h>

extern void asmisr_schedtimer(arch_regs*);
extern void asmisr_general(arch_regs*);
extern void asmisr_except(arch_regs*);
extern void asmisr_pagefault(arch_regs*);


#endif
