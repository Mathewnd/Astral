#ifndef _CPU_H
#define _CPU_H

#include <stddef.h>
#include <stdint.h>

#include <arch/msr.h>
#include <arch/ist.h>
#include <arch/archint.h>
#include <kernel/interrupt.h>
#include <kernel/vmm.h>
#include <kernel/timer.h>

typedef struct {
	uint64_t gdt[7];
	ist_t ist;
	long id;
	isr_t isr[MAX_ISR_COUNT];
	vmmcontext_t *vmmctx;
	int acpiid;
	timer_t *timer;
	bool intstatus;
	long ipl;
} cpu_t;

static inline cpu_t *_cpu() {
	return (cpu_t *)rdmsr(MSR_GSBASE);
}

static inline void cpu_set(cpu_t *ptr) {
	wrmsr(MSR_GSBASE, (uint64_t)ptr);
}

#endif
