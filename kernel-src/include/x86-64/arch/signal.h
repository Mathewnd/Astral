#ifndef _ARCH_SIGNAL_H
#define _ARCH_SIGNAL_H

#include <kernel/signal.h>

#define ARCH_SIGNAL_STACK_GROWS_DOWNWARDS 1
#define ARCH_SIGNAL_REDZONE_SIZE 128
#define ARCH_SIGNAL_SYSCALL_INSTRUCTION_SIZE 2

#define ARCH_SIGNAL_GETFROMRETURN(x) 

#define MCONTEXT_REG_R8 0
#define MCONTEXT_REG_R9 1
#define MCONTEXT_REG_R10 2
#define MCONTEXT_REG_R11 3
#define MCONTEXT_REG_R12 4
#define MCONTEXT_REG_R13 5
#define MCONTEXT_REG_R14 6
#define MCONTEXT_REG_R15 7
#define MCONTEXT_REG_RDI 8
#define MCONTEXT_REG_RSI 9
#define MCONTEXT_REG_RBP 10
#define MCONTEXT_REG_RBX 11
#define MCONTEXT_REG_RDX 12
#define MCONTEXT_REG_RAX 13
#define MCONTEXT_REG_RCX 14
#define MCONTEXT_REG_RSP 15
#define MCONTEXT_REG_RIP 16
#define MCONTEXT_REG_EFL 17
#define MCONTEXT_REG_CSGSFS 18
#define MCONTEXT_REG_ERR 19
#define MCONTEXT_REG_TRAPNO 20
#define MCONTEXT_REG_OLDMASK 21
#define MCONTEXT_REG_CR2 22
#define MCONTEXT_NGREG 23

typedef struct {
	unsigned long gregs[MCONTEXT_NGREG];
	uint8_t *fpu_state_p;
	unsigned long __reserved1[8];
} mcontext_t;

// this structure should have a part of it matching the ucontext from the abi
// which then gets its address returned by the ARCH_SIGFRAME_GET_UCONTEXT_POINTER macro
typedef struct {
	void *restorer; // for return from signal

	// ucontext starts here
	unsigned long uc_flags; // can be zero
	void *uc_link; // unused for signals
	stack_t oldstack;
	mcontext_t mcontext;
	sigset_t oldmask;

	// astral bits
	context_t context;
	extracontext_t extracontext;
	siginfo_t siginfo;
	uint8_t xsave[];
} sigframe_t;

#define ARCH_SIGFRAME_GET_UCONTEXT_POINTER(x) (&(x)->uc_flags)
#define ARCH_SIGFRAME_SIZE (sizeof(sigframe_t) + arch_xsave_size)

static inline void arch_sigframe_init(sigframe_t *sigframe) {
	sigframe->extracontext.xsave = sigframe->xsave;
}

#define CS_sig(context)      (*((WORD *)&(context)->uc_mcontext.gregs[REG_CSGSFS] + 0))
#define GS_sig(context)      (*((WORD *)&(context)->uc_mcontext.gregs[REG_CSGSFS] + 1))
#define FS_sig(context)      (*((WORD *)&(context)->uc_mcontext.gregs[REG_CSGSFS] + 2))
#define SS_sig(context)      (*((WORD *)&(context)->uc_mcontext.gregs[REG_CSGSFS] + 3))


static inline void arch_sigframe_prepare_mcontext(void *stack, sigframe_t *sigframe) {
	context_t *context = &sigframe->context;
	extracontext_t *extracontext = &sigframe->extracontext;
	unsigned long *gregs = sigframe->mcontext.gregs;

	gregs[MCONTEXT_REG_R8] = context->r8;
	gregs[MCONTEXT_REG_R9] = context->r9;
	gregs[MCONTEXT_REG_R10] = context->r10;
	gregs[MCONTEXT_REG_R11] = context->r11;
	gregs[MCONTEXT_REG_R12] = context->r12;
	gregs[MCONTEXT_REG_R13] = context->r13;
	gregs[MCONTEXT_REG_R14] = context->r14;
	gregs[MCONTEXT_REG_R15] = context->r15;
	gregs[MCONTEXT_REG_RDI] = context->rdi;
	gregs[MCONTEXT_REG_RSI] = context->rsi;
	gregs[MCONTEXT_REG_RBP] = context->rbp;
	gregs[MCONTEXT_REG_RBX] = context->rbx;
	gregs[MCONTEXT_REG_RDX] = context->rdx;
	gregs[MCONTEXT_REG_RAX] = context->rax;
	gregs[MCONTEXT_REG_RCX] = context->rcx;
	gregs[MCONTEXT_REG_RSP] = context->rsp;
	gregs[MCONTEXT_REG_RIP] = context->rip;
	gregs[MCONTEXT_REG_EFL] = context->rflags;
	gregs[MCONTEXT_REG_CSGSFS] = (uint64_t)context->cs | ((uint64_t)context->gs << 16) | ((uint64_t)context->fs << 32) | ((uint64_t)context->ss << 48);
	gregs[MCONTEXT_REG_ERR] = context->error;
	gregs[MCONTEXT_REG_TRAPNO] = context->irq;
	gregs[MCONTEXT_REG_OLDMASK] = 0;
	gregs[MCONTEXT_REG_CR2] = context->cr2;

	extracontext->xsave = ((sigframe_t *)stack)->xsave;
	sigframe->mcontext.fpu_state_p = extracontext->xsave;
}

static inline void arch_sigframe_get_context_from_mcontext(sigframe_t *sigframe) {
	context_t *context = &sigframe->context;
	extracontext_t *extracontext = &sigframe->extracontext;
	unsigned long *gregs = sigframe->mcontext.gregs;

	context->r8 = gregs[MCONTEXT_REG_R8];
	context->r9 = gregs[MCONTEXT_REG_R9];
	context->r10 = gregs[MCONTEXT_REG_R10];
	context->r11 = gregs[MCONTEXT_REG_R11];
	context->r12 = gregs[MCONTEXT_REG_R12];
	context->r13 = gregs[MCONTEXT_REG_R13];
	context->r14 = gregs[MCONTEXT_REG_R14];
	context->r15 = gregs[MCONTEXT_REG_R15];
	context->rdi = gregs[MCONTEXT_REG_RDI];
	context->rsi = gregs[MCONTEXT_REG_RSI];
	context->rbp = gregs[MCONTEXT_REG_RBP];
	context->rbx = gregs[MCONTEXT_REG_RBX];
	context->rdx = gregs[MCONTEXT_REG_RDX];
	context->rax = gregs[MCONTEXT_REG_RAX];
	context->rcx = gregs[MCONTEXT_REG_RCX];
	context->rsp = gregs[MCONTEXT_REG_RSP];
	context->rip = gregs[MCONTEXT_REG_RIP];
	context->rflags = gregs[MCONTEXT_REG_EFL];
	context->error = gregs[MCONTEXT_REG_ERR];
	context->cr2 = gregs[MCONTEXT_REG_CR2];
	context->cs = gregs[MCONTEXT_REG_CSGSFS] & 0xffff;
	context->gs = (gregs[MCONTEXT_REG_CSGSFS] >> 16) & 0xffff;
	context->fs = (gregs[MCONTEXT_REG_CSGSFS] >> 32) & 0xffff;
	context->ss = (gregs[MCONTEXT_REG_CSGSFS] >> 48) & 0xffff;
}


#endif
