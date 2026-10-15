#include <resource_allocator.h>
#include <arch/cpu.h>
#include <util.h>

void resource_allocator_init(resource_allocator_t *allocator, size_t resource_total, size_t allocate_max) {
	SPINLOCK_INIT(allocator->spinlock);
	list_init(&allocator->queue);
	allocator->resource_current = resource_total;
	allocator->allocate_max = allocate_max;
}

size_t resource_allocate(resource_allocator_t *allocator, size_t count) {
	count = min(allocator->allocate_max, count);
	bool status = spinlock_acquire_irq_clear(&allocator->spinlock);

	if (!list_is_empty(&allocator->queue) || allocator->resource_current < count) {
		resource_allocator_waiter_t waiter = {
			.requested = count,
			.thread = current_thread()
		};

		list_push_back(&allocator->queue, &waiter.list_node);
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

	while (!list_is_empty(&allocator->queue)) {
		resource_allocator_waiter_t *waiter = container_of(list_get_first(&allocator->queue), resource_allocator_waiter_t, list_node);
		if (waiter->requested > allocator->resource_current)
			break;
		allocator->resource_current -= waiter->requested;

		list_remove(&waiter->list_node);
		sched_wakeup(waiter->thread, 0);
	}
	
	spinlock_release_irq_restore(&allocator->spinlock, status);
}
