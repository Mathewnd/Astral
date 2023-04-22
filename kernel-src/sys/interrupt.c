#include <kernel/interrupt.h>
#include <logging.h>
#include <arch/cpu.h>
#include <arch/context.h>
#include <panic.h>

void interrupt_isr(int vec, context_t *ctx) {
	isr_t *isr = &_cpu()->isr[vec];
	if (isr->func == NULL)
		_panic("Unregistered interrupt", ctx);

	// TODO ipl check
	isr->func(isr, ctx);

	if (isr->eoi)
		isr->eoi(isr);
}

void interrupt_register(int vector, void (*func)(isr_t *self, context_t *ctx), void (*eoi)(isr_t *self), long priority) {
	isr_t *isr = &_cpu()->isr[vector];
	isr->func = func;
	isr->eoi = eoi;
	isr->id = (uint64_t)_cpu()->id << 32 | vector;
	isr->priority = priority;
}

isr_t *interrupt_allocate(void (*func)(isr_t *self, context_t *ctx), void (*eoi)(isr_t *self), long priority) {
	isr_t *isr = NULL;

	for (int i = 0; i < MAX_ISR_COUNT; ++i) {
		if (_cpu()->isr[i].func == NULL) {
			isr = &_cpu()->isr[i];
			interrupt_register(i, func, eoi, priority);
			break;
		}
	}

	return isr;
}

// TODO ipl is thread level, write these when the scheduler is written

void interrupt_raiseipl(long newipl) {
	__assert(!"Unimplemented");
}

void interrupt_loweripl(long newipl) {
	__assert(!"Unimplemented");
}
