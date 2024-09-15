#include <kernel/interrupt.h>
#include <logging.h>
#include <arch/cpu.h>
#include <arch/context.h>
#include <panic.h>

void arch_interrupt_disable();
void arch_interrupt_enable();

#define DOPENDING_SAVE() arch_context_saveandcall(dopending, NULL, NULL)

static void removefromqueue(isr_t *isr) {
	if (isr->prev)
		isr->prev->next = isr->next;
	else
		current_cpu()->isrqueue = isr->next;

	if (isr->next)
		isr->next->prev = isr->prev;
}

static void insertinqueue(isr_t *isr) {
	if (isr->pending)
		return;

	isr->next = current_cpu()->isrqueue;
	if (isr->next)
		isr->next->prev = isr;
	current_cpu()->isrqueue = isr;
}

static void runisr(isr_t *isr, context_t *ctx) {
		long oldipl = -1;
		if (isr->priority != IPL_IGNORE)
			oldipl = interrupt_raiseipl(isr->priority);

		isr->func(isr, ctx);
		interrupt_set(false);

		if (isr->priority != IPL_IGNORE)
			interrupt_loweripl(oldipl);
}

static void dopending(context_t *ctx, void *) {
	arch_interrupt_disable();
	bool entrystatus = current_cpu()->intstatus;
	current_cpu()->intstatus = false;

	isr_t *list = NULL;
	isr_t *iterator = current_cpu()->isrqueue;

	// build a list of pending ISRs and remove them from the main queue
	while (iterator) {
		isr_t *next = iterator->next;
		if (current_cpu()->ipl > iterator->priority) {
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

		runisr(isr, ctx);

		// if any new interrupts are pending, they will be taken care of here
		// XXX this is kinda hacky could be very very bad if there are a lot of interrupts
		dopending(ctx, NULL);
	}

	cleanup:
	if (entrystatus) {
		current_cpu()->intstatus = true;
		arch_interrupt_enable();
	}
}

__attribute__((no_caller_saved_registers)) void sched_userspacecheck(context_t *context, bool syscall, uint64_t syscallret, uint64_t syscallerrno);

void interrupt_isr(int vec, context_t *ctx) {
	isr_t *isr = &current_cpu()->isr[vec];
	current_cpu()->intstatus = false;

	if (isr->func == NULL)
		_panic("Unregistered interrupt", ctx);

	if (current_cpu()->ipl > isr->priority) {
		runisr(isr, ctx);
		dopending(ctx, NULL);
	} else {
		insertinqueue(isr);
		isr->pending = true;
	}

	if (isr->eoi)
		isr->eoi(isr);

	if (ARCH_CONTEXT_ISUSER(ctx))
		sched_userspacecheck(ctx, false, 0, 0);

	current_cpu()->intstatus = ARCH_CONTEXT_INTSTATUS(ctx);
}

void interrupt_raise(isr_t *isr) {
	bool entrystate = interrupt_set(false);

	if (isr->pending)
		goto cleanup;

	insertinqueue(isr);
	isr->pending = true;

	cleanup:
	interrupt_set(entrystate);
}

void interrupt_register(int vector, void (*func)(isr_t *self, context_t *ctx), void (*eoi)(isr_t *self), long priority) {
	bool intstatus = interrupt_set(false);

	isr_t *isr = &current_cpu()->isr[vector];
	isr->func = func;
	isr->eoi = eoi;
	isr->id = (uint64_t)current_cpu_id() | vector;
	isr->priority = priority;
	isr->pending = false;

	interrupt_set(intstatus);
}

isr_t *interrupt_allocate(void (*func)(isr_t *self, context_t *ctx), void (*eoi)(isr_t *self), long priority) {
	bool intstatus = interrupt_set(false);
	isr_t *isr = NULL;

	for (int i = 0; i < MAX_ISR_COUNT; ++i) {
		if (current_cpu()->isr[i].func == NULL) {
			isr = &current_cpu()->isr[i];
			interrupt_register(i, func, eoi, priority);
			break;
		}
	}

	interrupt_set(intstatus);
	return isr;
}

long interrupt_loweripl(long ipl) {
	bool oldintstatus = interrupt_set(false);
	long oldipl = current_cpu()->ipl;
	if (oldipl < ipl)
		current_cpu()->ipl = ipl;
	interrupt_set(oldintstatus);

	if (oldintstatus)
		DOPENDING_SAVE();

	return oldipl;
}

long interrupt_raiseipl(long ipl) {
	bool oldintstatus = interrupt_set(false);
	long oldipl = current_cpu()->ipl;

	if (oldipl > ipl)
		current_cpu()->ipl = ipl;

	interrupt_set(oldintstatus);

	return oldipl;
}

bool interrupt_set(bool status) {
	arch_interrupt_disable();
	bool old = current_cpu()->intstatus;
	current_cpu()->intstatus = status;

	if (status) {
		arch_interrupt_enable();
		DOPENDING_SAVE();
	}

	return old;
}
