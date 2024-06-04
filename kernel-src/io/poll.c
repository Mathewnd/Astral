#include <kernel/poll.h>
#include <arch/cpu.h>
#include <kernel/alloc.h>
#include <logging.h>
#include <kernel/interrupt.h>

static void insertinheader(pollheader_t *header, polldata_t *data) {
	data->prev = NULL;
	data->next = header->data;

	if (header->data)
		header->data->prev = data;

	header->data = data;
}

static void removefromlist(polldata_t **list, polldata_t *data) {
	if (data->next)
		data->next->prev = data->prev;

	if (data->prev)
		data->prev->next = data->next;
	else
		*list = data->next;

	data->prev = NULL;
	data->next = NULL;
}

int poll_initdesc(polldesc_t *desc, size_t size) {
	__assert(size);

	memset(desc, 0, sizeof(polldesc_t));

	desc->data = alloc(sizeof(polldata_t) * size);
	if (desc->data == NULL)
		return ENOMEM;

	desc->thread = _cpu()->thread;
	desc->size = size;
	SPINLOCK_INIT(desc->lock);
	SPINLOCK_INIT(desc->eventlock);
	SPINLOCK_INIT(desc->wakeuplock);
	for (uintmax_t i = 0; i < size; ++i)
		desc->data[i].desc = desc;
	__assert(spinlock_try(&desc->lock));

	return 0;
}

void poll_add(pollheader_t *header, polldata_t *data, int events) {
	bool intstate = interrupt_set(false);
	spinlock_acquire(&header->lock);
	data->events = events;
	data->header = header;
	insertinheader(header, data);
	spinlock_release(&header->lock);
	interrupt_set(intstate);
}

void poll_leave(polldesc_t *desc) {
	for (uintmax_t i = 0; i < desc->size; ++i) {
		pollheader_t *header = desc->data[i].header;
		if (header == NULL)
			continue;

		bool intstate = interrupt_set(false);
		spinlock_acquire(&header->lock);
		removefromlist(&header->data, &desc->data[i]);
		spinlock_release(&header->lock);
		interrupt_set(intstate);
	}
}

static void timeout(context_t *, dpcarg_t arg) {
	polldesc_t *desc = arg;

	bool intstate = interrupt_set(false);
	spinlock_acquire(&desc->eventlock);

	if (spinlock_try(&desc->lock))
		sched_wakeup(desc->thread, 1);

	spinlock_release(&desc->eventlock);
	interrupt_set(intstate);
}

int poll_dowait(polldesc_t *desc, size_t ustimeout) {
	timerentry_t sleepentry = {0};

	bool intstate = interrupt_set(false);
	spinlock_acquire(&desc->eventlock);
	if (desc->event) {
		spinlock_release(&desc->eventlock);
		interrupt_set(intstate);
		return 0;
	}

	sched_preparesleep(true);

	if (ustimeout != 0) {
		sched_targetcpu(_cpu());
		timer_insert(_cpu()->timer, &sleepentry, timeout, desc, ustimeout, false);
	}

	spinlock_release(&desc->lock);
	spinlock_release(&desc->eventlock);
	int ret = sched_yield();
	// on return, the desc lock is REQUIRED to be left locked by the poll_event function.
	// in the case of sleep being interrupted, wait until poll_event has done everything (if anything)
	__assert(ret || spinlock_try(&desc->lock) == false);

	if (ret == SCHED_WAKEUP_REASON_INTERRUPTED) {
		spinlock_acquire(&desc->wakeuplock);
		ret = EINTR;
	}

	if (ustimeout != 0 && ret != 1) {
		timer_remove(_cpu()->timer, &sleepentry);
		sched_targetcpu(NULL);
	}

	interrupt_set(intstate);

	ret = ret == 1 ? 0 : ret;

	return ret;
}

void poll_event(pollheader_t *header, int events) {
	bool intstate = interrupt_set(false);
	spinlock_acquire(&header->lock);
	polldata_t *pending = header->data;
	header->data = NULL;
	polldata_t *iterator = pending;

	while (iterator) {
		polldesc_t *desc = iterator->desc;
		polldata_t *next = iterator->next;
		spinlock_acquire(&desc->eventlock);
		int revents = (iterator->events | POLLHUP | POLLERR) & events;

		if (revents == 0 || spinlock_try(&desc->lock) == false || spinlock_try(&desc->wakeuplock) == false) {
			removefromlist(&pending, iterator);
			insertinheader(header, iterator);
		}

		if (desc->event == NULL) {
			iterator->revents = revents;
			desc->event = iterator;
		}

		spinlock_release(&desc->eventlock);

		iterator = next;
	}

	iterator = pending;
	while (iterator) {
		polldata_t *next = iterator->next;
		sched_wakeup(iterator->desc->thread, 0);
		insertinheader(header, iterator);
		spinlock_release(&iterator->desc->wakeuplock);
		iterator = next;
	}

	spinlock_release(&header->lock);
	interrupt_set(intstate);
}

void poll_destroydesc(polldesc_t *desc) {
	free(desc->data);
}
