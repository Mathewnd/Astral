#ifndef _CLS_H_INCLUDE
#define _CLS_H_INCLUDE

#include <arch/gdt.h>
#include <arch/ist.h>
#include <arch/ist.h>
#include <arch/regs.h>
#include <kernel/vmm.h>

// cpu level storage
// this will be pointed to by GS and will contain per cpu info

typedef struct{
	gdt_t gdt;
	ist_t ist;
	arch_regserror *laststate;
	vmm_context *context;
} cls_t;

void bsp_setcls();
void arch_setcls(cls_t* addr);
cls_t* arch_getcls();

static cls_t __seg_gs *cls = 0;


#endif
