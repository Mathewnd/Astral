#include <kernel/scheduler.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <logging.h>
#include <kernel/topology.h>

// set = idle
bitmap_t sched_idle_cpu_bitmap;
spinlock_t sched_idle_cpu_bitmap_lock;

static cpu_t *pick_cpu(thread_t *thread) {
	// if there is only one cpu awake, thats the current one
	if (arch_smp_cpusawake == 1)
		return current_cpu();

	// if the thread targets a specific cpu, then go into that
	if (thread->cputarget)
		return thread->cputarget;

	// find the closest cpu in the hierarchy that the thread can run immediatelly in
	cpu_t *closest = NULL;
	if (thread->last_cpu)
		closest = topology_find_next_cpu_to_run(thread->last_cpu->topology_node, thread, thread->metrics.last_sleep_duration_us);
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
		if (least_loaded == NULL || smp_cpus[i]->thread_count < least_loaded->thread_count)
			least_loaded = smp_cpus[i];
		
		if (sched_thread_can_run_in_cpu(thread, smp_cpus[i]->last_queue, smp_cpus[i]->last_interactivity) &&
				(least_loaded_can_run == NULL || least_loaded_can_run->thread_count > smp_cpus[i]->thread_count))
			least_loaded_can_run = smp_cpus[i];
	}

	return least_loaded_can_run ? least_loaded_can_run : least_loaded;
}

static void insert_in_run_queue(sched_run_queue_t *run_queue, thread_t *thread) {
	int idx = sched_thread_run_queue_index(thread->metrics.interactivity_score);

	thread->prev = run_queue->queues[idx].ins;

	if (thread->prev)
		thread->prev->next = thread;
	else
		run_queue->queues[idx].run = thread;

	thread->next = NULL;
	run_queue->queues[idx].ins = thread;

	bitmap_set(&run_queue->thread_bitmap, idx, 1);
}

static thread_t *pop_from_run_queue(sched_run_queue_t *run_queue) {
	long idx = bitmap_find_first_set(&run_queue->thread_bitmap);
	if (idx == -1)
		return NULL;

	thread_t *thread = run_queue->queues[idx].run;
	
	run_queue->queues[idx].run = thread->next;

	if (thread->next) {
		thread->next->prev = NULL;
	} else {
		run_queue->queues[idx].ins = NULL;
		bitmap_set(&run_queue->thread_bitmap, idx, 0);
	}

	return thread;
}

static thread_t *steal_from_calendar_queue(sched_calendar_queue_t *calendar_queue) {
	if (calendar_queue->thread_count == 0)
		return NULL;

	int run = calendar_queue->run;
	
	thread_t *thread = NULL;
	do {
		thread = calendar_queue->queues[run];
		thread_t *prev = NULL;
		while (thread) {
			if (thread->cputarget) {
				prev = thread;
				thread = thread->next;
				continue;
			}

			if (prev)
				prev->next = thread->next;
			else
				calendar_queue->queues[run] = thread->next;

			--calendar_queue->thread_count;
			return thread;
		}

		run = (run + 1) % SCHED_RUN_QUEUE_SIZE;
	} while (run != calendar_queue->run);

	// no thread found to steal
	return NULL;
}

static thread_t *steal_from_run_queue(sched_run_queue_t *run_queue) {
	long idx = bitmap_find_first_set(&run_queue->thread_bitmap);
	if (idx == -1)
		return NULL;

	for (; idx < SCHED_RUN_QUEUE_SIZE; ++idx) {
		for (thread_t *search = run_queue->queues[idx].run; search; search = search->next) {
			if (search->cputarget)
				continue;

			if (search->next) {
				search->next->prev = search->prev;
			} else {
				run_queue->queues[idx].ins = search->prev;
			}

			if (search->prev) {
				search->prev->next = search->next;
			} else {
				run_queue->queues[idx].run = search->next;
			}

			if (run_queue->queues[idx].run == NULL)
				bitmap_set(&run_queue->thread_bitmap, idx, 0);

			return search;
		}
	}

	// no thread found in this runqueue in which we can steal (only cpu targetting ones were found)
	return NULL;
}

static void insert_in_calendar_queue(sched_calendar_queue_t *calendar_queue, thread_t *thread) {
	int idx = (calendar_queue->ins + thread->metrics.interactivity_score - SCHED_INTERACTIVITY_LIMIT) % SCHED_RUN_QUEUE_SIZE;

	thread->next = calendar_queue->queues[idx];
	calendar_queue->queues[idx] = thread;
	++calendar_queue->thread_count;
}

static thread_t *pop_from_calendar_queue(sched_calendar_queue_t *calendar_queue) {
	if (calendar_queue->thread_count == 0)
		return NULL;

	thread_t *thread;

	do {
		thread = calendar_queue->queues[calendar_queue->run];
		if (thread)
			calendar_queue->queues[calendar_queue->run] = thread->next;
		else
			calendar_queue->run = (calendar_queue->run + 1) % SCHED_RUN_QUEUE_SIZE;

		if (calendar_queue->run == calendar_queue->ins)
			calendar_queue->ins = (calendar_queue->ins + 1) % SCHED_RUN_QUEUE_SIZE;
	} while (thread == NULL);

	--calendar_queue->thread_count;

	return thread;
}

// requires cpu queue to be locked
thread_t *sched_steal_work_from_cpu(cpu_t *cpu) {
	thread_t *thread = steal_from_run_queue(&cpu->rt_queue);
	if (thread)
		goto found;

	thread = steal_from_calendar_queue(&cpu->ts_queue);
	if (thread)
		goto found;

	thread = steal_from_run_queue(&cpu->idle_queue);
	found:

	--cpu->thread_count;
	--cpu->stealable_thread_count;
	return thread;
}

// requires cpu queue to be locked
thread_t *sched_select_next_thread(void) {
	thread_t *thread = pop_from_run_queue(&current_cpu()->rt_queue);
	if (thread) {
		current_cpu()->last_queue = 0;
		goto found;
	}

	thread = pop_from_calendar_queue(&current_cpu()->ts_queue);
	if (thread) {
		current_cpu()->last_queue = 1;
		goto found;
	}

	thread = pop_from_run_queue(&current_cpu()->idle_queue);
	if (thread == NULL)
		return NULL;
	current_cpu()->last_queue = 2;
found:
	thread->flags &= ~THREAD_FLAGS_QUEUED;

	current_cpu()->last_interactivity = thread->metrics.interactivity_score;
	thread->last_cpu = current_cpu();
	--current_cpu()->thread_count;
	if (thread->cputarget == NULL)
		--current_cpu()->stealable_thread_count;

	__assert((thread->flags & THREAD_FLAGS_QUEUED) == 0);
	__assert((thread->flags & THREAD_FLAGS_RUNNING) == 0);
	return thread;
}

// requires cpu queue to be locked
void sched_insert_in_cpu_queue(cpu_t *cpu, thread_t *thread) {
	__assert((thread->flags & THREAD_FLAGS_RUNNING) == 0);
	thread->flags |= THREAD_FLAGS_QUEUED;

	switch (thread->class) {
		case THREAD_CLASS_TIMESHARE:
			if (thread->metrics.interactivity_score > SCHED_INTERACTIVITY_LIMIT) {
				// thread is not interactive
				insert_in_calendar_queue(&cpu->ts_queue, thread);
				break;
			}
		case THREAD_CLASS_REAL_TIME:
			insert_in_run_queue(&cpu->rt_queue, thread);
			break;
		case THREAD_CLASS_IDLE:
			insert_in_run_queue(&cpu->idle_queue, thread);
			
			break;
	}

	++cpu->thread_count;
	if (thread->cputarget == NULL)
		++cpu->stealable_thread_count;

	spinlock_acquire(&sched_idle_cpu_bitmap_lock);

	bitmap_set(&sched_idle_cpu_bitmap, cpu->internal_id, 0);

	spinlock_release(&sched_idle_cpu_bitmap_lock);
}

void sched_queue(thread_t *thread) {
	bool status = interrupt_set(false);
	cpu_t *cpu = pick_cpu(thread);

	spinlock_acquire(&cpu->sched_lock);

	__assert((thread->flags & THREAD_FLAGS_QUEUED) == 0 && (thread->flags & THREAD_FLAGS_RUNNING) == 0);

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
	if (!smp_cpus)
		return;

	interrupt_set(false);
	cpu_t *most_loaded = NULL;
	cpu_t *least_loaded = NULL;

	// find the least loaded and most loaded cpus
	for (int i = 0; i < arch_smp_cpusawake; ++i) {
		cpu_t *old_most_loaded = NULL;
		cpu_t *old_least_loaded = NULL;

		spinlock_acquire(&smp_cpus[i]->sched_lock);
		if ((most_loaded == NULL || most_loaded->thread_count < smp_cpus[i]->thread_count) && smp_cpus[i]->stealable_thread_count) {
			old_most_loaded = most_loaded;
			most_loaded = smp_cpus[i];
		}

		if (least_loaded == NULL || least_loaded->thread_count > smp_cpus[i]->thread_count) {
			old_least_loaded = least_loaded;
			least_loaded = smp_cpus[i];
		}

		if (old_most_loaded)
			spinlock_release(&old_most_loaded->sched_lock);

		if (old_least_loaded && old_least_loaded != most_loaded && old_least_loaded != old_most_loaded)
			spinlock_release(&old_least_loaded->sched_lock);

		if (least_loaded != smp_cpus[i] && most_loaded != smp_cpus[i])
			spinlock_release(&smp_cpus[i]->sched_lock);
	}

	if (most_loaded == NULL || most_loaded->thread_count <= 1 || most_loaded->thread_count == least_loaded->thread_count)
		goto leave;

	thread_t *thread = sched_steal_work_from_cpu(most_loaded);
	__assert(thread);

	sched_insert_in_cpu_queue(least_loaded, thread);

	if (sched_thread_can_run_in_cpu(thread, least_loaded->last_queue, least_loaded->last_interactivity))
		sched_preempt_cpu(least_loaded);

	leave:
	if (most_loaded)
		spinlock_release(&most_loaded->sched_lock);
	if (most_loaded != least_loaded)
		spinlock_release(&least_loaded->sched_lock);
}
