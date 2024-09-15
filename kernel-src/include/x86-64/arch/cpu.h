#ifndef _CPU_H
#define _CPU_H

#include <stddef.h>
#include <stdint.h>

#include <arch/msr.h>
#include <arch/ist.h>
#include <kernel/interrupt.h>
#include <kernel/vmm.h>
#include <kernel/timer.h>
#include <kernel/scheduler.h>
#include <kernel/dpc.h>
#include <arch/apic.h>

#define ARCH_EOI arch_apic_eoi

typedef struct cpu_t {
	thread_t *thread;
	struct cpu_t *self;
	uint64_t gdt[7];
	ist_t ist;
	long id;
	isr_t isr[MAX_ISR_COUNT];
	vmmcontext_t *vmmctx;
	int acpiid;
	timer_t *timer;
	bool intstatus;
	long ipl;
	thread_t *idlethread;
	timerentry_t schedtimerentry;
	void *schedulerstack;
	isr_t *isrqueue;
	dpc_t *dpcqueue;
	isr_t *dpcisr;
} cpu_t;

#define CPU_HALT() asm volatile("hlt")
#define CPU_PAUSE() asm volatile("pause")

static inline uint32_t cpu_to_be_d(uint32_t d) {
	return __builtin_bswap32(d);
}

static inline uint16_t cpu_to_be_w(uint16_t w) {
	return __builtin_bswap16(w);
}

static inline uint32_t be_to_cpu_d(uint32_t d) {
	return __builtin_bswap32(d);
}

static inline uint16_t be_to_cpu_w(uint16_t w) {
	return __builtin_bswap16(w);
}

cpu_t *_bsp(void);

static inline cpu_t *_cpu(void) {
	cpu_t *cpu;
	asm volatile ("mov %%gs:8, %%rax" : "=a"(cpu) : : "memory");
	return cpu;
}

static inline thread_t *current_thread(void) {
	thread_t *thread;
	asm volatile ("mov %%gs:0, %%rax" : "=a"(thread) : : "memory");
	return thread;
}

static inline void cpu_set(cpu_t *ptr) {
	ptr->self = ptr;
	wrmsr(MSR_GSBASE, (uint64_t)ptr);
}

void cpu_initstate();

#endif
