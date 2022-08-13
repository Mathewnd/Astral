#ifndef _TIMER_H_INCLUDE
#define _TIMER_H_INCLUDE

#include <stddef.h>
#include <arch/regs.h>

typedef struct _timer_req{
	// values callee should care about
	void* (*func)(arch_regs*, void*);
	void* argptr;
	// internal values
	struct _timer_req* next;
	size_t ticks;
} timer_req;

void timer_init();
void timer_add(timer_req* req, size_t us);
void timer_resume();
void timer_stop();
void timer_irq(arch_regs* ctx);

#endif
