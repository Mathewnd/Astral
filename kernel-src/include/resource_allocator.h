#ifndef _RESOURCE_ALLOCATOR_H
#define _RESOURCE_ALLOCATOR_H

// simple FIFO allocator for an arbritary resource

#include <kernel/scheduler.h>
#include <mutex.h>

// TODO move this to a proper dequeue abstraction
typedef struct resource_allocator_waiter_t {
	size_t requested;
	thread_t *thread;
	struct resource_allocator_waiter_t *next;
} resource_allocator_waiter_t;

typedef struct {
	mutex_t mutex;
	resource_allocator_waiter_t *queue;
	resource_allocator_waiter_t *tail;
	size_t resource_current;
	size_t allocate_max;
} resource_allocator_t;

void resource_allocator_init(resource_allocator_t *allocator, size_t resource_total, size_t allocate_max);
void resource_allocate(resource_allocator_t *allocator, size_t count);
void resource_free(resource_allocator_t *allocator, size_t count);

#endif
