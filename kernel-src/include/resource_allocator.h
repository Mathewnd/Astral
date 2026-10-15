#ifndef _RESOURCE_ALLOCATOR_H
#define _RESOURCE_ALLOCATOR_H

// simple FIFO allocator for an arbritary resource

#include <kernel/scheduler.h>
#include <list.h>
#include <spinlock.h>

typedef struct resource_allocator_waiter_t {
	size_t requested;
	thread_t *thread;
	list_node_t list_node;
} resource_allocator_waiter_t;

typedef struct {
	spinlock_t spinlock; // a spinlock is used to allow freeing in an interrupt context
	list_node_t queue;
	size_t resource_current;
	size_t allocate_max;
} resource_allocator_t;

void resource_allocator_init(resource_allocator_t *allocator, size_t resource_total, size_t allocate_max);
size_t resource_allocate(resource_allocator_t *allocator, size_t count);
void resource_free(resource_allocator_t *allocator, size_t count);

#endif
