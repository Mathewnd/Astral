#ifndef _PANIC_H_INCLUDE
#define _PANIC_H_INCLUDE

#include <arch/regs.h>

__attribute__((noreturn)) void _panic(char *reason, arch_regserror *reg);

#endif
