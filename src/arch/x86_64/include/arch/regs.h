#ifndef _REGS_H_INCLUDE
#define _REGS_H_INCLUDE

#include <stdint.h>
#include <stdbool.h>

typedef uint64_t arch_reg;

typedef struct{
	uint64_t gsbase, fsbase;
	uint8_t  fx[512];
} arch_extraregs;

typedef struct{
        uint64_t cr2,gs,fs,es,ds,rax,rbx,rcx,rdx,r8,r9,r10,r11,r12,r13,r14,r15,rdi,rsi,rbp,error,rip,cs,rflags,rsp,ss;
} arch_regs;
void arch_regs_saveextra(arch_extraregs* regs);
void arch_regs_setupextra(arch_extraregs* regs);
void arch_regs_setupkernel(arch_regs* regs, void* ip, void* stack, bool interrupts);
void arch_regs_setupuser(arch_regs* regs, void* ip, void* stack, bool interrupts);

static inline void arch_regs_setret(arch_regs* regs, arch_reg reg){
	regs->rax = reg;
}

static inline void arch_regs_seterrno(arch_regs* regs, arch_reg reg){
	regs->rdx = reg;
}

#endif
