#ifndef _WORK_H
#define _WORK_H

#include <list.h>
#include <spinlock.h>
#include <semaphore.h>
#include <kernel/scheduler.h>

typedef void (*work_callback_t)(void *, size_t);

typedef struct {
	const char *name;
	spinlock_t lock;
	list_t work_list;
	list_t active_list;
	list_t waiter_list;
	semaphore_t pending_work;
	thread_t **threads;
	size_t thread_count;
	long lock_ipl;
	size_t seq;
} work_queue_t;

typedef struct {
	list_node_t list_node;
	work_callback_t callback;
	void *context;
	size_t pending;
} work_t;

#define WORK_INIT(work, cb, ctx) { \
	(work)->callback = cb; \
	(work)->context = ctx; \
	(work)->pending = 0; \
}	

extern work_queue_t *work_sharedq;

// creates a work queue with 'thread_count' threads with a spinlock at 'lock_ipl'
work_queue_t *work_queue_create(const char *name, size_t thread_count, long lock_ipl);

// enqueues work. work may be enqueued several times and even while work is still running
// work must not be enqueued across different queues
void work_enqueue(work_queue_t *wq, work_t *work);

// waits for one invocation of the work to complete. if many instances of the same work are running,
// wait for the first one
void work_wait(work_queue_t *wq, work_t *work);

// waits for at least all *currently pending* work to finish running.
void work_drain(work_queue_t *wq);

// dequeues work. if the work is not pending, this is a no-op
// if the work is running, return EBUSY
int work_dequeue(work_queue_t *wq, work_t *work);

#endif
