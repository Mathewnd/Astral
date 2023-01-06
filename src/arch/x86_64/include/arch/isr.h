#ifndef _ISR_H_INCLUDE
#define _ISR_H_INCLUDE

#include <arch/regs.h>

extern void asmisr_timer();
extern void asmisr_general();
extern void asmisr_except();
extern void asmisr_pagefault();
extern void asmisr_panic();
extern void asmisr_lapicnmi();
extern void asmisr_mmuinval();
extern void asmisr_ps2mouse();
extern void asmisr_ps2kbd();
extern void asmisr_timer();
extern void asmisr_simd();
extern void asmisr_nm();
extern void asmisr_gpf();

#endif
