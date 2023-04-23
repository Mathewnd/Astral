#ifndef _INTERRUPT_H
#define _INTERRUPT_H

#include <stdbool.h>

#include <arch/context.h>

#ifdef __x86_64__
	#define MAX_ISR_COUNT 256
#else
	#error Unsupported architecture!
#endif

#define IPL_MAX  -1
#define IPL_NONE 0
#define IPL_TIMER 1
#define IPL_BASE 1000

struct _isr_t;
typedef struct _isr_t {
	void (*func)(struct _isr_t *self, context_t *ctx);
	void (*eoi)(struct _isr_t *self);
	long id;
	long priority;
	bool pending;
} isr_t;

void interrupt_register(int vector, void (*func)(isr_t *self, context_t *ctx), void (*eoi)(isr_t *self), long priority);
isr_t *interrupt_allocate(void (*func)(isr_t *self, context_t *ctx), void (*eoi)(isr_t *self), long priority);
long interrupt_setipl(long newipl);
bool interrupt_set(bool status);

#endif
