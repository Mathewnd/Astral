#ifndef _REGS_H_INCLUDE
#define _REGS_H_INCLUDE

#include <stdint.h>
#include <stdbool.h>

typedef struct{
        uint64_t cr2,gs,fs,es,ds,rax,rbx,rcx,rdx,r8,r9,r10,r11,r12,r13,r14,r15,rdi,rsi,rbp,error,rip,cs,rflags,rsp,ss;
} arch_regs;

void arch_regs_setupkernel(arch_regs* regs, void* ip, void* stack, bool interrupts);

#endif
