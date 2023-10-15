#ifndef _INTERRUPT_H
#define _INTERRUPT_H

#include <stdbool.h>

#include <arch/context.h>

#ifdef __x86_64__
	#define MAX_ISR_COUNT 256
	#define INTERRUPT_IDTOVECTOR(id) ((id) & 0xffffffff)
#else
	#error Unsupported architecture!
#endif

#define IPL_IGNORE  -1
#define IPL_MAX 0
#define IPL_TIMER 1
#define IPL_KEYBOARD 2
#define IPL_NET 400
#define IPL_DISK 500
#define IPL_DPC 900
#define IPL_NORMAL 1000

struct _isr_t;
typedef struct _isr_t {
	struct _isr_t *next;
	struct _isr_t *prev;
	void (*func)(struct _isr_t *self, context_t *ctx);
	void (*eoi)(struct _isr_t *self);
	long id;
	long priority;
	bool pending;
} isr_t;

void interrupt_register(int vector, void (*func)(isr_t *self, context_t *ctx), void (*eoi)(isr_t *self), long priority);
isr_t *interrupt_allocate(void (*func)(isr_t *self, context_t *ctx), void (*eoi)(isr_t *self), long priority);
long interrupt_loweripl(long newipl);
long interrupt_raiseipl(long ipl);
bool interrupt_set(bool status);
void interrupt_raise(isr_t *isr);

#endif
