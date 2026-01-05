#include <resource_allocator.h>
#include <arch/cpu.h>

static void insert(resource_allocator_t *allocator, resource_allocator_waiter_t *node) {
	if (allocator->queue) {
		allocator->tail->next = node;
		allocator->tail = node;
	} else {
		allocator->queue = node;
		allocator->tail = node;
	}
}

static void pop(resource_allocator_t *allocator) {
	resource_allocator_waiter_t *w = allocator->queue;
	allocator->queue = w->next;
	sched_wakeup(w->thread, 0);
}

void resource_allocator_init(resource_allocator_t *allocator, size_t resource_total, size_t allocate_max) {
	MUTEX_INIT(&allocator->mutex);
	allocator->queue = allocator->tail = NULL;
	allocator->resource_current = resource_total;
	allocator->allocate_max = allocate_max;
}

void resource_allocate(resource_allocator_t *allocator, size_t count) {
	MUTEX_ACQUIRE(&allocator->mutex);

	if (allocator->queue || allocator->resource_current < count) {
		resource_allocator_waiter_t waiter = {
			.requested = count,
			.thread = current_thread(),
			.next = NULL
		};

		insert(allocator, &waiter);
		sched_prepare_sleep(false);
		MUTEX_RELEASE(&allocator->mutex);
		sched_yield();
		return;
	}

	allocator->resource_current -= count;
	
	MUTEX_RELEASE(&allocator->mutex);
}

void resource_free(resource_allocator_t *allocator, size_t count) {
	count = min(allocator->allocate_max, count);
	MUTEX_ACQUIRE(&allocator->mutex);

	allocator->resource_current += count;

	while (allocator->queue && allocator->queue->requested <= allocator->resource_current) {
		allocator->resource_current -= allocator->queue->requested;

		pop(allocator);
	}
	
	MUTEX_RELEASE(&allocator->mutex);
}
