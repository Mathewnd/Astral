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
#include <kernel/timekeeper.h>

#define ARCH_EOI arch_apic_eoi

typedef struct cpu_t {
	// expected to be exposed to the rest of the kernel
	thread_t *thread; // expected to be here by other code

	struct cpu_t *self; // expected to be here by other code

	vmmcontext_t *vmmctx; // expected to be here by other code

	long id; // expected to be here by other code

	timekeeper_source_t *timekeeper_source;
	timekeeper_source_info_t *timekeeper_source_info;
	time_t timekeeper_source_base_ticks;
	time_t timekeeper_source_tick_offset;

	isr_t isr[MAX_ISR_COUNT];
	bool intstatus;
	long ipl;
	isr_t *isrqueue;

	timer_t *timer;

	isr_t *dpcisr;
	dpc_t *dpcqueue;

	thread_t *idlethread;
	timerentry_t schedtimerentry;
	void *schedulerstack;

	dpc_t  reschedule_dpc;
	isr_t *reschedule_isr;

	// architecture specific, does not need to be exposed

	uint64_t gdt[7];
	char vendor[13];
	ist_t ist;
	int acpiid;

	uint32_t cpuid_max;

	int topology_thread;
	int topology_core;
	int topology_package;
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

cpu_t *get_bsp(void);

static inline thread_t *current_thread(void) {
	thread_t *thread;
	asm volatile ("mov %%gs:0, %%rax" : "=a"(thread) : : "memory");
	return thread;
}

static inline cpu_t *current_cpu(void) {
	cpu_t *cpu;
	asm volatile ("mov %%gs:8, %%rax" : "=a"(cpu) : : "memory");
	return cpu;
}

static inline vmmcontext_t *current_vmm_context(void) {
	vmmcontext_t *context;
	asm volatile ("mov %%gs:16, %%rax" : "=a"(context) : : "memory");
	return context;
}

static inline void set_current_vmm_context(vmmcontext_t *context) {
	asm volatile ("mov %%rax, %%gs:16" : : "a"(context) : "memory");
}

static inline long current_cpu_id(void) {
	long id;
	asm volatile ("mov %%gs:24, %%rax" : "=a"(id) : : "memory");
	return id;
}

static inline void cpu_set(cpu_t *ptr) {
	ptr->self = ptr;
	wrmsr(MSR_GSBASE, (uint64_t)ptr);
}

void cpu_initstate();

#endif
