#include <kernel/scheduler.h>
#include <arch/cpu.h>
#include <logging.h>

#define INTERACTIVITY_LIMIT 30

// set = idle
bitmap_t sched_idle_cpu_bitmap;
spinlock_t sched_idle_cpu_bitmap_lock;

static cpu_t *pick_cpu(thread_t *thread) {
	if (thread->cputarget)
		return thread->cputarget;

	return current_cpu();
}

static void insert_in_run_queue(sched_run_queue_t *run_queue, thread_t *thread) {
	int idx = min(thread->metrics.interactivity_score * SCHED_RUN_QUEUE_SIZE / SCHED_MAX_INTERACTIVITY, SCHED_RUN_QUEUE_SIZE - 1);

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

static void insert_in_calendar_queue(sched_calendar_queue_t *calendar_queue, thread_t *thread) {
	int idx = (calendar_queue->ins + thread->metrics.interactivity_score - INTERACTIVITY_LIMIT) % SCHED_RUN_QUEUE_SIZE;

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

		calendar_queue->run = (calendar_queue->run + 1) % SCHED_RUN_QUEUE_SIZE;
		if (calendar_queue->run == calendar_queue->ins)
			calendar_queue->ins = (calendar_queue->ins + 1) % SCHED_RUN_QUEUE_SIZE;
	} while (thread == NULL);

	--calendar_queue->thread_count;

	return thread;
}

// requires cpu queue to be locked
thread_t *sched_select_next_thread(void) {
	thread_t *thread = pop_from_run_queue(&current_cpu()->rt_queue);
	if (thread)
		goto found;

	thread = pop_from_calendar_queue(&current_cpu()->ts_queue);
	if (thread)
		goto found;

	thread = pop_from_run_queue(&current_cpu()->idle_queue);
	if (thread == NULL)
		return NULL;
found:
	thread->flags &= ~THREAD_FLAGS_QUEUED;
	return thread;
}

// requires cpu queue to be locked
void sched_insert_in_cpu_queue(cpu_t *cpu, thread_t *thread) {
	__assert((thread->flags & THREAD_FLAGS_RUNNING) == 0);
	thread->flags |= THREAD_FLAGS_QUEUED;

	switch (thread->class) {
		case THREAD_CLASS_TIMESHARE:
			if (thread->metrics.interactivity_score > INTERACTIVITY_LIMIT) {
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

	spinlock_acquire(&sched_idle_cpu_bitmap_lock);

	bitmap_set(&sched_idle_cpu_bitmap, cpu->internal_id, 0);

	spinlock_release(&sched_idle_cpu_bitmap_lock);
}

void sched_queue(thread_t *thread) {
	bool status = interrupt_set(false);
	cpu_t *cpu = pick_cpu(thread);

	spinlock_acquire(&cpu->sched_lock);

	__assert((thread->flags & THREAD_FLAGS_QUEUED) == 0 && (thread->flags & THREAD_FLAGS_RUNNING) == 0);
	sched_insert_in_cpu_queue(cpu, thread);
	
	spinlock_release_irq_restore(&cpu->sched_lock, status);
}

// ran every 10 ms per core
void sched_calendar_tick(context_t *, dpcarg_t) {
	spinlock_acquire(&current_cpu()->sched_lock);

	current_cpu()->ts_queue.ins = (current_cpu()->ts_queue.ins + 1) % SCHED_RUN_QUEUE_SIZE;

	spinlock_release(&current_cpu()->sched_lock);
}
