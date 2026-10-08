#ifdef PIAB_TEST_HOOKS
#include "../../tests/piab_hooks.h"
#endif
#include <kernel/scheduler.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <logging.h>
#include <kernel/topology.h>

// set = idle
bitmap_t sched_idle_cpu_bitmap;
spinlock_t sched_idle_cpu_bitmap_lock;

static cpu_t *pick_cpu(thread_t *thread) {
	// if the thread targets a specific cpu, then go into that
	if (thread->cputarget)
		return thread->cputarget;

	// if the system is still being bootstrapped or it is a single core machine, run it on the BSP
	if (arch_smp_get_cpu_count() == 1 || unlikely(arch_smp_cpusawake != arch_smp_get_cpu_count()))
		return get_bsp();

	// find the closest cpu in the hierarchy that the thread can run immediatelly in
	cpu_t *closest = NULL;
	if (thread->last_cpu) {
		closest = topology_find_next_cpu_to_run(thread->last_cpu->topology_node, thread,
		                                        thread->metrics.last_sleep_duration_us);
	}
	if (closest)
		return closest;

	// find the first idle cpu
	spinlock_acquire(&sched_idle_cpu_bitmap_lock);
	long idle_cpu = bitmap_find_first_set(&sched_idle_cpu_bitmap);
	spinlock_release(&sched_idle_cpu_bitmap_lock);

	if (idle_cpu != -1)
		return smp_cpus[idle_cpu];

	// find the least loaded cpu and the least loaded one that the thread can run immediatelly in
	cpu_t *least_loaded = NULL;
	cpu_t *least_loaded_can_run = NULL;

	// deliberately racey to reduce contention
	// TODO: possibly could benefit from making the last queue and interactivity a single integer which would allow
	// atomic operations on these
	for (int i = 0; i < arch_smp_cpusawake; ++i) {
		if (least_loaded == NULL || smp_cpus[i]->thread_count < least_loaded->thread_count) {
			least_loaded = smp_cpus[i];
		}

		if (sched_thread_can_run_in_cpu(thread, smp_cpus[i]->last_queue,
		                                smp_cpus[i]->last_interactivity) &&
		    (least_loaded_can_run == NULL ||
		     least_loaded_can_run->thread_count > smp_cpus[i]->thread_count)) {
			least_loaded_can_run = smp_cpus[i];
		}
	}

	return least_loaded_can_run ? least_loaded_can_run : least_loaded;
}

static thread_t *sched_pop_from_run_queue(sched_run_queue_t *run_queue) {
	long idx = bitmap_find_first_set(&run_queue->thread_bitmap);
	if (idx == -1)
		return NULL;

	list_node_t *node = list_pop_front(&run_queue->queues[idx]);
	if (list_is_empty(&run_queue->queues[idx]))
		bitmap_set(&run_queue->thread_bitmap, idx, 0);

	return container_of(node, thread_t, queue_node);
}

static thread_t *sched_steal_from_run_queue(sched_run_queue_t *run_queue) {
	long idx = bitmap_find_first_set(&run_queue->thread_bitmap);
	if (idx == -1)
		return NULL;

	for (; idx < SCHED_RUN_QUEUE_SIZE; ++idx) {
		list_for_each (&run_queue->queues[idx], node) {
			thread_t *thread = container_of(node, thread_t, queue_node);
			if (thread->cputarget)
				continue;

			list_remove(node);
			if (list_is_empty(&run_queue->queues[idx]))
				bitmap_set(&run_queue->thread_bitmap, idx, 0);

			return thread;
		}
	}

	// no thread found in this runqueue in which we can steal (only cpu targetting ones were found)
	return NULL;
}

static thread_t *sched_pop_from_calendar_queue(sched_calendar_queue_t *calendar_queue) {
	if (calendar_queue->thread_count == 0)
		return NULL;

	list_node_t *node;

	do {
		node = list_pop_front(&calendar_queue->queues[calendar_queue->run]);
		if (node == NULL)
			calendar_queue->run = (calendar_queue->run + 1) % SCHED_RUN_QUEUE_SIZE;

		if (calendar_queue->run == calendar_queue->ins)
			calendar_queue->ins = (calendar_queue->ins + 1) % SCHED_RUN_QUEUE_SIZE;
	} while (node == NULL);

	--calendar_queue->thread_count;
	return container_of(node, thread_t, queue_node);
}

static thread_t *sched_steal_from_calendar_queue(sched_calendar_queue_t *calendar_queue) {
	if (calendar_queue->thread_count == 0)
		return NULL;

	int run = calendar_queue->run;

	do {
		list_for_each (&calendar_queue->queues[run], node) {
			thread_t *thread = container_of(node, thread_t, queue_node);
			if (thread->cputarget == NULL) {
				list_remove(node);
				--calendar_queue->thread_count;
				return thread;
			}
		}

		run = (run + 1) % SCHED_RUN_QUEUE_SIZE;
	} while (run != calendar_queue->run);

	return NULL;
}

// requires cpu queue to be locked
thread_t *sched_steal_work_from_cpu(cpu_t *cpu) {
	thread_t *thread = sched_steal_from_run_queue(&cpu->rt_queue);
	if (thread)
		goto found;

	thread = sched_steal_from_calendar_queue(&cpu->ts_queue);
	if (thread)
		goto found;

	thread = sched_steal_from_run_queue(&cpu->idle_queue);
	found:

	if (thread == NULL)
		return NULL;

	--cpu->thread_count;
	--cpu->stealable_thread_count;
	return thread;
}

bool sched_idle_steal_work(void) {
	bool intstatus = interrupt_set(false);
	cpu_t *destination = current_cpu();
	bool work = false;

	spinlock_acquire(&destination->sched_lock);
	work = destination->thread_count != 0;
	spinlock_release(&destination->sched_lock);
	if (work)
		goto leave;

	size_t cpu_count = arch_smp_get_cpu_count();
	if (cpu_count == 1 || unlikely(arch_smp_cpusawake != cpu_count))
		goto leave;

	for (size_t offset = 1; offset < cpu_count; ++offset) {
		cpu_t *source = smp_cpus[(destination->internal_id + offset) % cpu_count];
		cpu_t *first = source->internal_id < destination->internal_id ? source : destination;
		cpu_t *second = first == source ? destination : source;

		if (!spinlock_try(&first->sched_lock))
			continue;

		if (!spinlock_try(&second->sched_lock)) {
			spinlock_release(&first->sched_lock);
			continue;
		}

		work = destination->thread_count != 0;
		if (!work && source->stealable_thread_count) {
			thread_t *thread = sched_steal_work_from_cpu(source);
			__assert(thread);
			sched_insert_in_cpu_queue(destination, thread);
			work = true;
		}

		spinlock_release(&second->sched_lock);
		spinlock_release(&first->sched_lock);
		if (work)
			break;
	}

leave:
	interrupt_set(intstatus);
	return work;
}

// requires cpu queue to be locked
thread_t *sched_select_next_thread(void) {
	sched_priority_t priority;
	thread_t *thread = sched_pop_from_run_queue(&current_cpu()->rt_queue);
	if (thread) {
		goto found;
	}

	thread = sched_pop_from_calendar_queue(&current_cpu()->ts_queue);
	if (thread) {
		goto found;
	}

	thread = sched_pop_from_run_queue(&current_cpu()->idle_queue);
	if (thread == NULL)
		return NULL;
found:
	thread->flags &= ~THREAD_FLAGS_QUEUED;

	priority = sched_thread_priority(thread);
	current_cpu()->last_queue = sched_priority_queue(priority);
	current_cpu()->last_interactivity = sched_priority_score(priority);
	thread->last_cpu = current_cpu();
	--current_cpu()->thread_count;
	if (thread->cputarget == NULL)
		--current_cpu()->stealable_thread_count;

	if (thread == current_cpu()->idlethread) {
		spinlock_acquire(&sched_idle_cpu_bitmap_lock);
		bitmap_set(&sched_idle_cpu_bitmap, current_cpu()->internal_id, 1);
		spinlock_release(&sched_idle_cpu_bitmap_lock);
	}

	__assert((thread->flags & THREAD_FLAGS_RUNNING) == 0);
	return thread;
}

static inline void sched_insert_in_queue(cpu_t *cpu, thread_t *thread, sched_priority_t base,
                                         sched_priority_t priority) {
	unsigned queue_class = sched_priority_queue(priority);
	unsigned effective_score = sched_priority_score(priority);
	unsigned idx;

	thread->queued_priority = priority;

	if (queue_class == SCHED_QUEUE_TIMESHARE) {
		sched_calendar_queue_t *calendar_queue = &cpu->ts_queue;
		idx = calendar_queue->run;

		if (priority > base) {
			// The owner still needs the boost after a wakeup or migration.
			// Put it at the tail so yielding gives other threads a turn.
			list_push_back(&calendar_queue->queues[idx], &thread->queue_node);
		} else {
			unsigned range = SCHED_MAX_INTERACTIVITY - SCHED_INTERACTIVITY_LIMIT - 1;

			// Round up so scores just above the interactive limit advance past
			// ins instead of rounding down to the insertion queue.
			idx = effective_score - SCHED_INTERACTIVITY_LIMIT;
			idx *= SCHED_RUN_QUEUE_SIZE - 1;
			idx = (idx + range - 1) / range;

			idx += calendar_queue->ins;
			if (calendar_queue->ins < calendar_queue->run) {
				idx += SCHED_RUN_QUEUE_SIZE;
			}

			// Without the clamp a low priority thread can wrap back into a
			// queue that runs sooner.
			idx = min(idx, calendar_queue->run + SCHED_RUN_QUEUE_SIZE - 1);
			idx %= SCHED_RUN_QUEUE_SIZE;
			list_push_front(&calendar_queue->queues[idx], &thread->queue_node);
		}

		++calendar_queue->thread_count;
	} else {
		sched_run_queue_t *run_queue;

		if (queue_class == SCHED_QUEUE_REALTIME) {
			run_queue = &cpu->rt_queue;
		} else {
			run_queue = &cpu->idle_queue;
		}

		idx = sched_thread_run_queue_index(effective_score);
		list_push_back(&run_queue->queues[idx], &thread->queue_node);
		bitmap_set(&run_queue->thread_bitmap, idx, 1);
	}

	thread->queue_index = idx;
}

// requires cpu queue to be locked
void sched_insert_in_cpu_queue(cpu_t *cpu, thread_t *thread) {
	sched_priority_t base, floor, priority;

	__assert((thread->flags & THREAD_FLAGS_RUNNING) == 0);
	thread->flags |= THREAD_FLAGS_QUEUED;

	// Publish the destination before reading priority. An updater either
	// locks this CPU or changes priority before the loads below.
	__atomic_store_n(&thread->cpu, cpu, __ATOMIC_SEQ_CST);
#ifdef PIAB_TEST_HOOKS
	piab_test_cpu_published(thread);
#endif
	base = __atomic_load_n(&thread->base_priority, __ATOMIC_SEQ_CST);
	floor = __atomic_load_n(&thread->priority_floor, __ATOMIC_SEQ_CST);
	priority = max(base, floor);
	sched_insert_in_queue(cpu, thread, base, priority);

	++cpu->thread_count;
	if (thread->cputarget == NULL)
		++cpu->stealable_thread_count;

	spinlock_acquire(&sched_idle_cpu_bitmap_lock);

	bitmap_set(&sched_idle_cpu_bitmap, cpu->internal_id, 0);

	spinlock_release(&sched_idle_cpu_bitmap_lock);
}

void sched_queue(thread_t *thread) {
	bool status = interrupt_set(false);
	sched_update_base_priority(thread);
	cpu_t *cpu = pick_cpu(thread);

	spinlock_acquire(&cpu->sched_lock);

	__assert((thread->flags & THREAD_FLAGS_QUEUED) == 0 &&
	         (thread->flags & THREAD_FLAGS_RUNNING) == 0);

	int last_queue = cpu->last_queue;
	int last_interactivity = cpu->last_interactivity;
	sched_insert_in_cpu_queue(cpu, thread);

	if (sched_thread_can_run_in_cpu(thread, last_queue, last_interactivity))
		sched_preempt_cpu(cpu);

	spinlock_release_irq_restore(&cpu->sched_lock, status);
}

// ran every 10 ms per core
void sched_calendar_tick(context_t *, dpcarg_t) {
	interrupt_set(false);
	spinlock_acquire(&current_cpu()->sched_lock);

	current_cpu()->ts_queue.ins = (current_cpu()->ts_queue.ins + 1) % SCHED_RUN_QUEUE_SIZE;

	spinlock_release(&current_cpu()->sched_lock);
}

// ran every second TODO randomize
// takes a single thread from the most loaded cpu and gives it to the least loaded cpu
// note that there has to be a migrateable thread for this to be possible. 
// (so if a thread targets the cpu it is ignored in the count)
void sched_load_balancer(context_t *, dpcarg_t) {
	size_t cpu_count = arch_smp_get_cpu_count();
	if (cpu_count < 2 ||
	    __atomic_load_n(&arch_smp_cpusawake, __ATOMIC_ACQUIRE) != cpu_count)
		return;

	interrupt_set(false);
	cpu_t *most_loaded = NULL;
	cpu_t *least_loaded = NULL;
	size_t most_loaded_count = 0;
	size_t least_loaded_count = 0;

	// find the least loaded and most loaded cpus
	for (size_t i = 0; i < arch_smp_cpusawake; ++i) {
		spinlock_acquire(&smp_cpus[i]->sched_lock);
		size_t thread_count = smp_cpus[i]->thread_count;
		size_t stealable_thread_count = smp_cpus[i]->stealable_thread_count;
		spinlock_release(&smp_cpus[i]->sched_lock);

		if ((most_loaded == NULL || most_loaded_count < thread_count) && stealable_thread_count) {
			most_loaded = smp_cpus[i];
			most_loaded_count = thread_count;
		}

		if (least_loaded == NULL || least_loaded_count > thread_count) {
			least_loaded = smp_cpus[i];
			least_loaded_count = thread_count;
		}
	}

	if (most_loaded == NULL || most_loaded == least_loaded)
		return;

	cpu_t *first = most_loaded->internal_id < least_loaded->internal_id ? most_loaded : least_loaded;
	cpu_t *second = first == most_loaded ? least_loaded : most_loaded;
	spinlock_acquire(&first->sched_lock);
	spinlock_acquire(&second->sched_lock);

	// the queues may have changed since the scan, so only move work if it still reduces the imbalance
	if (most_loaded->stealable_thread_count == 0 || most_loaded->thread_count <= 1 ||
	    most_loaded->thread_count <= least_loaded->thread_count)
		goto leave;

	thread_t *thread = sched_steal_work_from_cpu(most_loaded);
	__assert(thread);

	sched_insert_in_cpu_queue(least_loaded, thread);

	if (sched_thread_can_run_in_cpu(thread, least_loaded->last_queue,
	                                least_loaded->last_interactivity)) {
		sched_preempt_cpu(least_loaded);
	}

	leave:
	spinlock_release(&second->sched_lock);
	spinlock_release(&first->sched_lock);
}

// Use saved placement as the current priority may already have changed
static void sched_requeue_thread(cpu_t *cpu, thread_t *thread, sched_priority_t base,
                                 sched_priority_t priority) {
	unsigned idx = thread->queue_index;
	unsigned queue_class = sched_priority_queue(thread->queued_priority);
	sched_run_queue_t *queue;

	__assert(idx < SCHED_RUN_QUEUE_SIZE);

	list_remove(&thread->queue_node);

	if (queue_class == SCHED_QUEUE_TIMESHARE) {
		--cpu->ts_queue.thread_count;
	} else {
		if (queue_class == SCHED_QUEUE_REALTIME) {
			queue = &cpu->rt_queue;
		} else {
			queue = &cpu->idle_queue;
		}

		if (list_is_empty(&queue->queues[idx]))
			bitmap_set(&queue->thread_bitmap, idx, 0);
	}

	sched_insert_in_queue(cpu, thread, base, priority);

	spinlock_acquire(&sched_idle_cpu_bitmap_lock);

	bitmap_set(&sched_idle_cpu_bitmap, cpu->internal_id, 0);

	spinlock_release(&sched_idle_cpu_bitmap_lock);
}

// Call after updating base_priority or priority_floor, without holding a CPU queue lock.
// Sleeping threads use the updated priority when sched_queue runs on wakeup.
void sched_priority_changed(thread_t *thread) {
	cpu_t *cpu;
	sched_priority_t base, floor, priority;
	unsigned queue, effective_score;
	bool boosted, changed, lowered;
	bool irq = interrupt_set(false);

	for (;;) {
		cpu = __atomic_load_n(&thread->cpu, __ATOMIC_SEQ_CST);
#ifdef PIAB_TEST_HOOKS
		piab_test_cpu_loaded(thread);
#endif
		if (cpu == NULL)
			break;

		// Retry if the thread migrated while we were acquiring the CPU lock
		spinlock_acquire(&cpu->sched_lock);
		if (__atomic_load_n(&thread->cpu, __ATOMIC_SEQ_CST) != cpu) {
			spinlock_release(&cpu->sched_lock);
			continue;
		}

		base = __atomic_load_n(&thread->base_priority, __ATOMIC_SEQ_CST);
		floor = __atomic_load_n(&thread->priority_floor, __ATOMIC_SEQ_CST);
		priority = max(base, floor);
		queue = sched_priority_queue(priority);
		boosted = priority > base;

		if (thread->flags & THREAD_FLAGS_QUEUED) {
			changed = priority != thread->queued_priority;

			if (queue == SCHED_QUEUE_TIMESHARE &&
			    sched_priority_queue(thread->queued_priority) == SCHED_QUEUE_TIMESHARE) {
				// Boosted threads can run now without waiting for the calendar
				if ((priority > thread->queued_priority || boosted) &&
				    thread->queue_index != (unsigned)cpu->ts_queue.run) {
					list_remove(&thread->queue_node);
					thread->queue_index = cpu->ts_queue.run;
					list_push_front(&cpu->ts_queue.queues[thread->queue_index],
					                &thread->queue_node);

					changed = true;
				}

				// Keep the thread's place if its priority drops before it runs
				thread->queued_priority = priority;
			} else if (changed) {
				sched_requeue_thread(cpu, thread, base, priority);
			}

			if (changed && sched_thread_can_run_in_cpu(thread, cpu->last_queue,
			                                           cpu->last_interactivity)) {
				sched_preempt_cpu(cpu);
			}
		} else if (cpu->thread == thread) {
			effective_score = sched_priority_score(priority);
			lowered = queue > cpu->last_queue ||
			          (queue == cpu->last_queue && effective_score > cpu->last_interactivity);

			cpu->last_queue = queue;
			cpu->last_interactivity = effective_score;

			// Only a decrease can put a queued thread ahead of this one
			if (lowered)
				sched_preempt_cpu(cpu);
		}

		spinlock_release(&cpu->sched_lock);
		break;
	}

	interrupt_set(irq);
}
