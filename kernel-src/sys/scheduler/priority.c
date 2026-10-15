#include <kernel/scheduler.h>
#include <logging.h>

// Recompute base priority from the scheduler metrics. Lock boosts don't
// change the thread's class or interactivity score.
void sched_update_base_priority(thread_t *thread) {
	unsigned interactivity_score = thread->metrics.interactivity_score;
	unsigned queue;
	sched_priority_t priority, previous;

	if (thread->class == THREAD_CLASS_IDLE) {
		queue = SCHED_QUEUE_IDLE;
	} else if (thread->class == THREAD_CLASS_TIMESHARE &&
	           interactivity_score > SCHED_INTERACTIVITY_LIMIT) {
		queue = SCHED_QUEUE_TIMESHARE;
	} else {
		queue = SCHED_QUEUE_REALTIME;
	}

	__assert(interactivity_score < SCHED_MAX_INTERACTIVITY);
	priority = SCHED_PRIORITY_MAX - queue * SCHED_MAX_INTERACTIVITY - interactivity_score;
	previous = __atomic_exchange_n(&thread->base_priority, priority, __ATOMIC_SEQ_CST);

	if (previous != priority)
		piab_thread_priority_changed(thread);
}

sched_priority_t sched_thread_priority(thread_t *thread) {
	sched_priority_t base = __atomic_load_n(&thread->base_priority, __ATOMIC_SEQ_CST);
	sched_priority_t floor = __atomic_load_n(&thread->priority_floor, __ATOMIC_SEQ_CST);

	return max(base, floor);
}

static void sched_assert_priority(sched_priority_t priority) {
	__assert(priority && priority <= SCHED_PRIORITY_MAX);
	__assert(sched_priority_queue(priority) != SCHED_QUEUE_TIMESHARE ||
	         sched_priority_score(priority) > SCHED_INTERACTIVITY_LIMIT);
}

// Replace one floor reference under priority_lock. Each lock keeps its own
// reference, even when several locks request the same priority.
bool sched_replace_priority_floor_locked(thread_t *thread, sched_priority_t old,
                                         sched_priority_t priority) {
	sched_priority_t maximum, previous, base;

	__assert(old || priority);
	if (old)
		sched_assert_priority(old);
	if (priority)
		sched_assert_priority(priority);

	if (old) {
		__assert(thread->priority_floor_counts[old]);
		--thread->priority_floor_counts[old];
	}

	if (priority) {
		__assert(thread->priority_floor_counts[priority] != UINT16_MAX);
		++thread->priority_floor_counts[priority];
	}

	previous = __atomic_load_n(&thread->priority_floor, __ATOMIC_RELAXED);
	maximum = max(previous, priority);

	// Only removing the highest floor needs a search
	while (maximum && thread->priority_floor_counts[maximum] == 0)
		--maximum;
	__atomic_store_n(&thread->priority_floor, maximum, __ATOMIC_SEQ_CST);

	// Floors hidden by base priority still need accounting, but neither
	// waiter keys nor cpu queue placement change when those floors change.
	// A concurrent base change either queues an update itself or is included
	// in this comparison.
	base = __atomic_load_n(&thread->base_priority, __ATOMIC_SEQ_CST);
	return max(base, previous) != max(base, maximum);
}

// Replace a floor and apply any effective priority change.
void sched_update_priority_floor(thread_t *thread, sched_priority_t old, sched_priority_t priority) {
	bool changed, irq;
	long ipl;

	ipl = interrupt_raiseipl(IPL_DPC);
	irq = spinlock_acquire_irq_clear(&thread->priority_lock);
	changed = sched_replace_priority_floor_locked(thread, old, priority);
	spinlock_release_irq_restore(&thread->priority_lock, irq);

	if (changed) {
		piab_update_thread_records(thread);
		sched_priority_changed(thread);
	}
	interrupt_loweripl(ipl);
}

// Add one floor reference. Pair it with clear_priority_floor at the same
// priority, even when another lock already gives the thread that floor.
void sched_set_priority_floor(thread_t *thread, sched_priority_t priority) {
	sched_update_priority_floor(thread, 0, priority);
}

// Remove one previously added floor reference. Other references at this
// priority remain in effect until their callers remove them.
void sched_clear_priority_floor(thread_t *thread, sched_priority_t priority) {
	sched_update_priority_floor(thread, priority, 0);
}
