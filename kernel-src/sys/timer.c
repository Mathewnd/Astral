#include <kernel/timer.h>
#include <kernel/alloc.h>
#include <logging.h>
#include <kernel/interrupt.h>

void timer_isr(timer_t *timer, context_t *context) {
	// TODO lock
	__assert(timer->queue);
	timer->tickcurrent = timer->queue->absolutetick;
	timerentry_t *oldentry = timer->queue;
	timer->queue = timer->queue->next;

	if (timer->queue) {
		// TODO handle this
		__assert(timer->queue->absolutetick > timer->tickcurrent);
		timer->arm(timer->queue->absolutetick - timer->tickcurrent);
	}

	// TODO unlock
	oldentry->func(oldentry->private, context);
}

void timer_insert(timer_t *timer, timerentry_t* entry, time_t us) {
	long oldipl = interrupt_setipl(IPL_TIMER);
	// TODO lock

	timer->tickcurrent += timer->stop(timer);

	time_t tickdiff = us * timer->ticksperus;
	entry->absolutetick = timer->tickcurrent + tickdiff;

	if (timer->queue == NULL) { // if no other entries in the queue
		entry->next = NULL;
		timer->queue = entry;
	}else if (timer->queue->absolutetick > entry->absolutetick) { // if the current entry should be put before all the others in the queue
		entry->next = timer->queue;
		timer->queue = entry;
	} else {
		timerentry_t *search = timer->queue;

		while(search) {
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

	// TODO handle this case properly
	__assert(timer->queue->absolutetick > timer->tickcurrent);
	timer->arm(timer->queue->absolutetick - timer->tickcurrent);

	// TODO unlock
	interrupt_setipl(oldipl);
}

timer_t *timer_new(time_t ticksperus, void (*arm)(time_t), time_t (*stop)()) {
	timer_t *timer = alloc(sizeof(timer_t));
	if (timer == NULL)
		return NULL;

	timer->ticksperus = ticksperus;
	timer->arm = arm;
	timer->stop = stop;

	return timer;
}
