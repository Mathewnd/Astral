#ifndef _CONTEXT_H
#define _CONTEXT_H

#include <stdint.h>
#include <printf.h>

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
	uint64_t rbp;
	uint64_t error;
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ss;
} __attribute__((packed)) context_t;

#define CTX_SP(x) x->rsp;
#define CTX_IP(x) x->rip;
#define PRINT_CTX(x) { \
	printf("cr2: %016lx  gs: %04x\nrax: %016lx  fs: %04x\nrbx: %016lx  es: %04x\nrcx: %016lx  ds: %04x\nrdx: %016lx  cs: %04x\n r8: %016lx  ss: %04x\n r9: %016lx r10: %016lx\nr11: %016lx r12: %016lx\nr13: %016lx r14: %016lx\nr15: %016lx rdi: %016lx\nrsi: %016lx rbp: %016lx\nrip: %016lx rsp: %016lx\nerr: %016lx rfl: %016lx\n", \
	x->cr2, x->gs, x->rax, x->fs, x->rbx, x->es, x->rcx, x->ds, x->rdx, x->cs, x->r8, x->ss, x->r9, x->r10, x->r11, x->r12, x->r13, x->r14, x->r15, x->rdi, x->rsi, x->rbp, x->rip, x->rsp, x->error, x->rflags); \
}

#endif