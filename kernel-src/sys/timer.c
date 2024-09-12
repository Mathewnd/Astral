#include <kernel/timer.h>
#include <kernel/alloc.h>
#include <logging.h>
#include <kernel/interrupt.h>

static void insert(timer_t *timer, timerentry_t *entry, time_t us) {
	entry->absolutetick = timer->tickcurrent + us * timer->ticksperus;
	// it overflowed, this will be so far into the future we can just
	// set it to be far off enough into the future that it really does not matter.
	if (entry->absolutetick < timer->tickcurrent)
		entry->absolutetick = timer->ticksperus * 1000 * 1000 * 60 * 60 * 24 * 30 * 12 * 1000; // a thousand years from now.

	if (timer->queue == NULL) { // if no other entries in the queue
		entry->next = NULL;
		timer->queue = entry;
	} else if (timer->queue->absolutetick > entry->absolutetick) { // if the current entry should be put before all the others in the queue
		__assert(entry->next == NULL);
		entry->next = timer->queue;
		timer->queue = entry;
	} else {
		timerentry_t *search = timer->queue;

		while (search) {
			__assert(search != entry);
			if (search->next == NULL) { // if last entry in the queue
				entry->next = NULL;
				search->next = entry;
				break;
			} else if (entry->absolutetick >= search->absolutetick && search->next->absolutetick > entry->absolutetick) { // insert between two
				entry->next = search->next;
				search->next = entry;
				break;
			}
			search = search->next;
		}
	}
}

// expects IPL to be at least IPL_TIMER
static void timercheck(timer_t *timer) {
	while (timer->queue && timer->queue->absolutetick == timer->tickcurrent) {
		timerentry_t *entry = timer->queue;
		timer->queue = timer->queue->next;
		entry->next = NULL;

		if (entry->repeatus)
			insert(timer, entry, entry->repeatus);
		else
			entry->fired = true;

		dpc_enqueue(&entry->dpc, entry->fn, entry->arg);
	}
}

// TODO if an entry is removed and the timer still fires,
// it will skip forwards in time which is bad BAD
void timer_isr(timer_t *timer, context_t *context) {
	spinlock_acquire(&timer->lock);

	__assert(timer->queue);

	timer->tickcurrent = timer->queue->absolutetick;
	timerentry_t *oldentry = timer->queue;
	timer->queue = timer->queue->next;
	oldentry->next = NULL;

	if (oldentry->repeatus)
		insert(timer, oldentry, oldentry->repeatus);
	else
		oldentry->fired = true;

	dpc_enqueue(&oldentry->dpc, oldentry->fn, oldentry->arg);

	if (timer->queue) {
		timercheck(timer);
		timer->arm(timer->queue->absolutetick - timer->tickcurrent);
	}

	spinlock_release(&timer->lock);
}

void timer_resume(timer_t *timer) {
	long oldipl = interrupt_raiseipl(IPL_TIMER);
	spinlock_acquire(&timer->lock);

	timer->running = true;

	if (timer->queue == NULL)
		goto leave;

	timercheck(timer);
	timer->arm(timer->queue->absolutetick - timer->tickcurrent);

	leave:
	spinlock_release(&timer->lock);
	interrupt_loweripl(oldipl);
}

void timer_stop(timer_t *timer) {
	long oldipl = interrupt_raiseipl(IPL_TIMER);
	spinlock_acquire(&timer->lock);

	timer->tickcurrent += timer->stop(timer);
	timer->running = false;

	spinlock_release(&timer->lock);
	interrupt_loweripl(oldipl);
}

void timer_insert(timer_t *timer, timerentry_t *entry, dpcfn_t fn, dpcarg_t arg, time_t us, bool repeating) {
	long oldipl = interrupt_raiseipl(IPL_TIMER);
	spinlock_acquire(&timer->lock);

	if (timer->running)
		timer->tickcurrent += timer->stop(timer);

	memset(entry, 0, sizeof(timerentry_t));
	entry->repeatus = repeating ? us : 0;
	entry->fn = fn;
	entry->arg = arg;
	entry->fired = false;
	insert(timer, entry, us);

	if (timer->running) {
		timercheck(timer);
		timer->arm(timer->queue->absolutetick - timer->tickcurrent);
	}

	spinlock_release(&timer->lock);
	interrupt_loweripl(oldipl);
}

uintmax_t timer_remove(timer_t *timer, timerentry_t *entry) {
	long oldipl = interrupt_raiseipl(IPL_TIMER);
	spinlock_acquire(&timer->lock);

	if (timer->running)
		timer->tickcurrent += timer->stop(timer);

	uintmax_t timeremaining = 0;

	// if it had already fired by the time the lock was reached
	if (entry->fired == true)
		goto cleanup;

	timerentry_t *iterator = timer->queue;
	timerentry_t *prev = NULL;

	while (iterator && iterator != entry) {
		prev = iterator;
		iterator = iterator->next;
	}

	__assert(iterator);

	if (prev == NULL)
		timer->queue = iterator->next;
	else
		prev->next = iterator->next;

	iterator->next = NULL;

	// ALWAYS round up the time remaining
	timeremaining = ROUND_UP(iterator->absolutetick - timer->tickcurrent, timer->ticksperus) / timer->ticksperus;

	if (timer->queue) {
		timercheck(timer);
		timer->arm(timer->queue->absolutetick - timer->tickcurrent);
	}

	cleanup:
	spinlock_release(&timer->lock);
	interrupt_loweripl(oldipl);
	return timeremaining;
}

timer_t *timer_new(time_t ticksperus, void (*arm)(time_t), time_t (*stop)()) {
	timer_t *timer = alloc(sizeof(timer_t));
	if (timer == NULL)
		return NULL;

	timer->ticksperus = ticksperus;
	timer->running = false;
	timer->arm = arm;
	timer->stop = stop;
	SPINLOCK_INIT(timer->lock);

	return timer;
}
