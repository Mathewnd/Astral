#include <kernel/itimer.h>
#include <logging.h>

static void itimer_dpc(context_t *context, dpcarg_t arg) {
	itimer_t *itimer = arg;
	spinlock_acquire(&itimer->lock);
	if (itimer->paused)
		goto leave;
	
	itimer->remainingus = itimer->repeatus ? itimer->repeatus : 0;
	itimer->paused = itimer->repeatus == 0;
	itimer->fn(context, itimer->arg);
	if (itimer->repeatus) {
		timer_insert(current_cpu()->timer, &itimer->entry, itimer_dpc, itimer, itimer->remainingus, false);
	}

	leave:
	spinlock_release(&itimer->lock);
}

void itimer_init(itimer_t *itimer, dpcfn_t fn, dpcarg_t arg) {
	memset(itimer, 0, sizeof(itimer_t));
	itimer->fn = fn;
	itimer->arg = arg;
	itimer->paused = true;
	SPINLOCK_INIT(itimer->lock);
}

void itimer_pause(itimer_t *itimer, uintmax_t *remainingus, uintmax_t *repeatus) {
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&itimer->lock);

	while (itimer->cpu && itimer->cpu != current_cpu()) {
		// schedule on the itimer cpu
		sched_targetcpu(itimer->cpu);
		spinlock_release(&itimer->lock);
		// allow interrupts to be handled (such as a tlb shootdown)
		// as the other cpu could be busy waiting for this one
		interrupt_set(true);
		interrupt_set(false);
		sched_yield();
		sched_targetcpu(NULL);
		spinlock_acquire(&itimer->lock);
	}

	if (itimer->paused)
		goto cleanup;

	itimer->cpu = NULL;
	itimer->remainingus = timer_remove(current_cpu()->timer, &itimer->entry);
	if (itimer->remainingus == 0) {
		// while we were running in the cpu, the timer fired.
		// the dpc won't be handled because the timer entry
		// has already been removed from the queue.
		// this will be handled as a ''timer was one microsecond away from firing''
		// situation as its the cleanest way of doing this.
		itimer->remainingus = 1;
	}
	itimer->paused = true;
	cleanup:

	spinlock_release(&itimer->lock);
	interrupt_set(intstatus);
	if (remainingus)
		*remainingus = itimer->remainingus;
	if (repeatus)
		*repeatus = itimer->repeatus;
}

void itimer_set(itimer_t *itimer, uintmax_t timerus, uintmax_t repeatus) {
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&itimer->lock);

	itimer->remainingus = timerus;
	itimer->repeatus = repeatus;

	spinlock_release(&itimer->lock);
	interrupt_set(intstatus);
}

void itimer_resume(itimer_t *itimer) {
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&itimer->lock);

	__assert(itimer->paused && itimer->remainingus);
	itimer->cpu = current_cpu();
	itimer->paused = false;
	timer_insert(current_cpu()->timer, &itimer->entry, itimer_dpc, itimer, itimer->remainingus, false);

	spinlock_release(&itimer->lock);
	interrupt_set(intstatus);
}
