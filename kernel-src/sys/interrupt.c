#include <kernel/interrupt.h>
#include <logging.h>
#include <arch/cpu.h>
#include <arch/context.h>
#include <panic.h>

void interrupt_isr(int vec, context_t *ctx) {
	isr_t *isr = &_cpu()->isr[vec];
	_cpu()->intstatus = false;

	if (isr->func == NULL)
		_panic("Unregistered interrupt", ctx);

	if (_cpu()->ipl > isr->priority) {
		long oldipl = interrupt_setipl(isr->priority);

		isr->func(isr, ctx);
		interrupt_set(false);

		interrupt_setipl(oldipl);
	} else {
		isr->pending = true;
	}

	if (isr->eoi)
		isr->eoi(isr);

	_cpu()->intstatus = ARCH_CONTEXT_INTSTATUS(ctx);
}

void interrupt_register(int vector, void (*func)(isr_t *self, context_t *ctx), void (*eoi)(isr_t *self), long priority) {
	isr_t *isr = &_cpu()->isr[vector];
	isr->func = func;
	isr->eoi = eoi;
	isr->id = (uint64_t)_cpu()->id << 32 | vector;
	isr->priority = priority;
	isr->pending = false;
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

long interrupt_setipl(long ipl) {
	bool oldintstatus = interrupt_set(false);
	long oldipl = _cpu()->ipl;
	_cpu()->ipl = ipl;
	interrupt_set(oldintstatus);

	// TODO do pending interrupts

	return oldipl;
}

void arch_interrupt_disable();
void arch_interrupt_enable();

bool interrupt_set(bool status) {
	if (status)
		arch_interrupt_enable();
	else
		arch_interrupt_disable();
	bool old = _cpu()->intstatus;
	_cpu()->intstatus = status;
	return old;
}
