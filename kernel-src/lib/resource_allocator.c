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
	SPINLOCK_INIT(allocator->spinlock);
	allocator->queue = allocator->tail = NULL;
	allocator->resource_current = resource_total;
	allocator->allocate_max = allocate_max;
}

size_t resource_allocate(resource_allocator_t *allocator, size_t count) {
	count = min(allocator->allocate_max, count);
	bool status = spinlock_acquire_irq_clear(&allocator->spinlock);

	if (allocator->queue || allocator->resource_current < count) {
		resource_allocator_waiter_t waiter = {
			.requested = count,
			.thread = current_thread(),
			.next = NULL
		};

		insert(allocator, &waiter);
		sched_prepare_sleep(false);
		spinlock_release(&allocator->spinlock);
		sched_yield();
		interrupt_set(status);
		return count;
	}

	allocator->resource_current -= count;
	
	spinlock_release_irq_restore(&allocator->spinlock, status);
	return count;
}

void resource_free(resource_allocator_t *allocator, size_t count) {
	bool status = spinlock_acquire_irq_clear(&allocator->spinlock);

	allocator->resource_current += count;

	while (allocator->queue && allocator->queue->requested <= allocator->resource_current) {
		allocator->resource_current -= allocator->queue->requested;

		pop(allocator);
	}
	
	spinlock_release_irq_restore(&allocator->spinlock, status);
}
