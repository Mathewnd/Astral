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

	desc->data = alloc(sizeof(polldata_t) * size);
	if (desc->data == NULL)
		return ENOMEM;

	desc->thread = _cpu()->thread;
	desc->size = size;
	SPINLOCK_INIT(desc->lock);
	MUTEX_INIT(&desc->eventlock);
	for (uintmax_t i = 0; i < size; ++i)
		desc->data[i].desc = desc;

	return 0;
}

void poll_add(pollheader_t *header, polldata_t *data, int events) {
	MUTEX_ACQUIRE(&header->lock, false);
	data->events = events;
	data->header = header;
	insertinheader(header, data);
	MUTEX_RELEASE(&header->lock);
}

void poll_leave(polldesc_t *desc) {
	for (uintmax_t i = 0; i < desc->size; ++i) {
		pollheader_t *header = desc->data[i].header;
		if (header == NULL)
			continue;

		MUTEX_ACQUIRE(&header->lock, false);
		removefromlist(&header->data, &desc->data[i]);
		MUTEX_RELEASE(&header->lock);
	}
}

static void timeout(context_t *, dpcarg_t arg) {
	polldesc_t *desc = arg;

	MUTEX_TRY(&desc->eventlock);

	if (spinlock_try(&desc->lock))
		sched_wakeup(desc->thread, 1);

	MUTEX_RELEASE(&desc->eventlock);
}

int poll_dowait(polldesc_t *desc, size_t ustimeout) {
	timerentry_t sleepentry = {0};

	MUTEX_ACQUIRE(&desc->eventlock, false);
	if (desc->event) {
		MUTEX_RELEASE(&desc->eventlock);
		return 0;
	}

	sched_preparesleep(true);

	if (ustimeout != 0) {
		sched_targetcpu(_cpu());
		timer_insert(_cpu()->timer, &sleepentry, timeout, desc, ustimeout, false);
	}

	spinlock_release(&desc->lock);
	MUTEX_RELEASE(&desc->eventlock);
	int ret = sched_yield();
	// on return, the desc lock is REQUIRED to be left locked by the poll_event function.
	// XXX interruptible wait breaks this
	__assert(spinlock_try(&desc->lock) == false);

	if (ustimeout != 0 && ret != 1) {
		timer_remove(_cpu()->timer, &sleepentry);
		sched_targetcpu(NULL);
	}

	ret = ret == 1 ? 0 : ret;

	return ret;
}

void poll_event(pollheader_t *header, int events) {
	MUTEX_ACQUIRE(&header->lock, false);
	polldata_t *pending = header->data;
	header->data = NULL;
	polldata_t *iterator = pending;

	while (iterator) {
		polldesc_t *desc = iterator->desc;
		polldata_t *next = iterator->next;
		MUTEX_ACQUIRE(&desc->eventlock, false);
		// TODO POLLHUP and POLLERR
		int revents = iterator->events & events;

		if (spinlock_try(&desc->lock) == false) {
			removefromlist(&pending, iterator);
			insertinheader(header, iterator);
		}

		if (revents && desc->event == NULL) {
			iterator->revents = revents;
			desc->event = iterator;
		}

		MUTEX_RELEASE(&desc->eventlock);

		iterator = next;
	}

	int ipl = interrupt_raiseipl(IPL_DPC);

	iterator = pending;
	while (iterator) {
		polldata_t *next = iterator->next;
		sched_wakeup(iterator->desc->thread, 0);
		insertinheader(header, iterator);
		iterator = next;
	}

	MUTEX_RELEASE(&header->lock);
	interrupt_loweripl(ipl);
}

void poll_destroydesc(polldesc_t *desc) {
	free(desc->data);
}
