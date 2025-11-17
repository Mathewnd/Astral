#ifndef _CONTEXT_H
#define _CONTEXT_H

#include <stdint.h>
#include <printf.h>

#include <arch/msr.h>

typedef uint64_t ctxreg_t;

typedef struct {
        uint64_t cr2;
	uint64_t gs;
	uint64_t fs;
	uint64_t es;
	uint64_t ds;
	uint64_t rax;
	uint64_t rbx;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t r8;
	uint64_t r9;
	uint64_t r10;
	uint64_t r11;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t irq;
	uint64_t rbp;
	uint64_t error;
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ss;
} __attribute__((packed)) context_t;

typedef struct {
	uint64_t gsbase;
	uint64_t fsbase;
	void *xsave;
} extracontext_t;

extern size_t arch_xsave_size;

#define CTX_INIT(x,u,interrupts) \
	if (u) { \
		(x)->cs = 0x23; \
		(x)->ds = (x)->es = (x)->ss = 0x1b; \
	} else { \
		(x)->cs = 0x8; \
		(x)->ds = (x)->es = (x)->ss = 0x10; \
	} \
	(x)->rflags = interrupts ? 0x200 : 0;

#define CTX_SP(x) (x)->rsp
#define CTX_IP(x) (x)->rip
#define CTX_RET(x) (x)->rax
#define CTX_ERRNO(x) (x)->rdx
#define CTX_TRAP_ADDR(x) (x)->cr2
#define CTX_ARG0(x) (x)->rdi
#define CTX_ARG1(x) (x)->rsi
#define CTX_ARG2(x) (x)->rdx

#define PRINT_CTX(x) { \
	printf("cr2: %016lx  gs: %04x\nrax: %016lx  fs: %04x\nrbx: %016lx  es: %04x\nrcx: %016lx  ds: %04x\nrdx: %016lx  cs: %04x\n r8: %016lx  ss: %04x\n r9: %016lx r10: %016lx\nr11: %016lx r12: %016lx\nr13: %016lx r14: %016lx\nr15: %016lx rdi: %016lx\nrsi: %016lx rbp: %016lx\nrip: %016lx rsp: %016lx\nerr: %016lx rfl: %016lx\n", \
	x->cr2, x->gs, x->rax, x->fs, x->rbx, x->es, x->rcx, x->ds, x->rdx, x->cs, x->r8, x->ss, x->r9, x->r10, x->r11, x->r12, x->r13, x->r14, x->r15, x->rdi, x->rsi, x->rbp, x->rip, x->rsp, x->error, x->rflags); \
}

void arch_context_switch(context_t *context);
int arch_context_saveandcall(void (*fn)(context_t *context, void *argument), void *stack, void *argument);

void arch_extracontext_detect(void);

int arch_extracontext_init(extracontext_t *context);
void arch_extracontext_free(extracontext_t *context);
void arch_extracontext_save(extracontext_t *context);
void arch_extracontext_load(extracontext_t *context);
void arch_extracontext_copy(extracontext_t *dst, const extracontext_t *src);

#include <string.h>

// kernelgsbase is set as user because they'll be swapped in the context switch
#define ARCH_CONTEXT_SWITCHTHREAD(x) \
	current_cpu()->ist.rsp0 = (uint64_t)(x)->kernelstacktop; \
	arch_extracontext_load(&(x)->extracontext); \
	arch_context_switch(&thread->context);

#define ARCH_CONTEXT_THREADSAVE(t, c) \
	memcpy(&(t)->context, c, sizeof(context_t)); \
	arch_extracontext_save(&(t)->extracontext);

#define ARCH_CONTEXT_THREADLOAD(t, c) \
	memcpy(c, &(t)->context, sizeof(context_t)); \
	current_cpu()->ist.rsp0 = (uint64_t)(t)->kernelstacktop; \
	arch_extracontext_load(&(t)->extracontext);

#define ARCH_EXTRACONTEXT_LOAD(c) \
	arch_extracontext_load(c);

#define ARCH_EXTRACONTEXT_SAVE(c) \
	arch_extracontext_save(c);

#define ARCH_CONTEXT_INTSTATUS(x) ((x)->rflags & 0x200 ? true : false)
#define ARCH_CONTEXT_ISUSER(x) ((x)->cs == 0x23)

#endif
