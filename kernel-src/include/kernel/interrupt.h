#ifndef _INTERRUPT_H
#define _INTERRUPT_H

#include <stdbool.h>

#ifdef __x86_64__
	#define MAX_ISR_COUNT 256
#else
	#error Unsupported architecture!
#endif

struct _isr_t;
typedef struct _isr_t {
	void (*func)(struct _isr_t *self); // TODO pass register context as argument as well.
	void (*eoi)(struct _isr_t *self);
	long id;
	long priority;
	bool pending;
} isr_t;

void interrupt_register(int vector, void (*func)(isr_t *self), void (*eoi)(isr_t *self), long priority); // TODO pass register context as well.
isr_t *interrupt_allocate(void (*func)(isr_t *self), void (*eoi)(isr_t *self), long priority); // TODO pass register context as well.
void interrupt_raiseipl(long newipl);
void interrupt_loweripl(long newipl);

#endif
