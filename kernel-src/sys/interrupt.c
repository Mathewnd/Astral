#include <kernel/interrupt.h>
#include <logging.h>
#include <arch/cpu.h>

// TODO pass register context as well
void interrupt_isr(int vec) {
	isr_t *isr = &_cpu()->isr[vec];
	// TODO ipl check
	isr->func(isr); // TODO pass context

	if (isr->eoi)
		isr->eoi(isr);
}

// TODO pass register context as well.
void interrupt_register(int vector, void (*func)(isr_t *self), void (*eoi)(isr_t *self), long priority) {
	isr_t *isr = &_cpu()->isr[vector];
	isr->func = func;
	isr->eoi = eoi;
	isr->id = _cpu()->id << 32 | vector;
	isr->priority = priority;
}

// TODO pass register context as well.
isr_t *interrupt_allocate(void (*func)(isr_t *self), void (*eoi)(isr_t *self), long priority) {
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
