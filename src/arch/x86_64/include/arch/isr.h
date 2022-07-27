#ifndef _ISR_H_INCLUDE
#define _ISR_H_INCLUDE

#include <arch/regs.h>

extern void asmisr_general(arch_regsnoerror*);
extern void asmisr_except(arch_regserror*);
extern void asmisr_pagefault(arch_regserror*);


#endif
