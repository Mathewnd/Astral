#include <kernel/interrupt.h>
#include <logging.h>
#include <arch/cpu.h>
#include <arch/context.h>
#include <panic.h>

void arch_interrupt_disable();
void arch_interrupt_enable();

#define DOPENDING_SAVE() arch_context_saveandcall(dopending, NULL)

static void removefromqueue(isr_t *isr) {
	if (isr->prev)
		isr->prev->next = isr->next;
	else
		_cpu()->isrqueue = isr->next;

	if (isr->next)
		isr->next->prev = isr->prev;
}

static void insertinqueue(isr_t *isr) {
	if (isr->pending)
		return;

	isr->next = _cpu()->isrqueue;
	if (isr->next)
		isr->next->prev = isr;
	_cpu()->isrqueue = isr;
}

static void dopending(context_t *ctx) {
	bool entrystatus = _cpu()->intstatus;
	if (entrystatus) {
		_cpu()->intstatus = false;
		arch_interrupt_disable();
	}

	isr_t *list = NULL;
	isr_t *iterator = _cpu()->isrqueue;

	// build a list of pending ISRs and remove them from the main queue

	while (iterator) {
		isr_t *next = iterator->next;
		if (_cpu()->ipl > iterator->priority) {
			removefromqueue(iterator);

			if (list)
				list->prev = iterator;
			iterator->next = list;
			iterator->prev = NULL;
			list = iterator;

			__assert(iterator->pending);
		}

		iterator = next;
	}

	if (list == NULL)
		goto cleanup;

	// run pending ISRs

	while (list) {
		isr_t *isr = list;
		list = list->next;
		if (list)
			list->prev = NULL;
		isr->pending = false;

		long oldipl = interrupt_raiseipl(isr->priority);

		isr->func(isr, ctx);
		interrupt_set(false);

		interrupt_loweripl(oldipl);

		// if any new interrupts are pending, they will be taken care of here
		// XXX this is kinda hacky could be very very bad if there are a lot of interrupts
		dopending(ctx);
	}

	cleanup:
	if (entrystatus) {
		_cpu()->intstatus = true;
		arch_interrupt_enable();
	}
}


void interrupt_isr(int vec, context_t *ctx) {
	isr_t *isr = &_cpu()->isr[vec];
	_cpu()->intstatus = false;

	if (isr->func == NULL)
		_panic("Unregistered interrupt", ctx);

	if (_cpu()->ipl > isr->priority) {
		long oldipl = interrupt_raiseipl(isr->priority);

		isr->func(isr, ctx);
		interrupt_set(false);

		interrupt_loweripl(oldipl);
		dopending(ctx);
	} else {
		insertinqueue(isr);
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

long interrupt_loweripl(long ipl) {
	bool oldintstatus = interrupt_set(false);
	long oldipl = _cpu()->ipl;
	if (oldipl < ipl)
		_cpu()->ipl = ipl;
	interrupt_set(oldintstatus);

	if (oldintstatus)
		DOPENDING_SAVE();

	return oldipl;
}

long interrupt_raiseipl(long ipl) {
	bool oldintstatus = interrupt_set(false);
	long oldipl = _cpu()->ipl;

	if (oldipl > ipl)
		_cpu()->ipl = ipl;

	interrupt_set(oldintstatus);

	return oldipl;
}

bool interrupt_set(bool status) {
	if (status)
		arch_interrupt_enable();
	else
		arch_interrupt_disable();

	bool old = _cpu()->intstatus;
	_cpu()->intstatus = status;

	if (status)
		DOPENDING_SAVE();

	return old;
}
