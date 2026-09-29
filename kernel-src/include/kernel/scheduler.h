#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include <kernel/proc.h>
#include <kernel/thread.h>
#include <util.h>
#include <bitmap.h>

#define SCHED_WAKEUP_REASON_NORMAL 0
#define SCHED_WAKEUP_REASON_INTERRUPTED -1

#define STACK_TOP (void *)0x0000800000000000
#define INTERP_BASE (void *)0x00000beef0000000

#define SCHED_RUN_QUEUE_SIZE 32

enum {
	SCHED_QUEUE_REALTIME,
	SCHED_QUEUE_TIMESHARE,
	SCHED_QUEUE_IDLE
};

// Timeshare threads above this use the calendar queue
#define SCHED_INTERACTIVITY_LIMIT 30

#define SCHED_NICE_MAX  19
#define SCHED_NICE_MIN -20
#define SCHED_SLEEP_TIME_LIMIT_CPU_US 1000

typedef struct {
	list_node_t queues[SCHED_RUN_QUEUE_SIZE];
	bitmap_t thread_bitmap;
} sched_run_queue_t;

typedef struct {
	list_node_t queues[SCHED_RUN_QUEUE_SIZE];
	int ins;
	int run;
	size_t thread_count;
} sched_calendar_queue_t;

extern bitmap_t sched_idle_cpu_bitmap;
extern spinlock_t sched_idle_cpu_bitmap_lock;

sched_priority_t sched_thread_priority(thread_t *thread);
void sched_update_base_priority(thread_t *thread);
void sched_priority_changed(thread_t *thread);

void sched_set_priority_floor(thread_t *thread, sched_priority_t priority);
void sched_clear_priority_floor(thread_t *thread, sched_priority_t priority);
void sched_update_priority_floor(thread_t *thread, sched_priority_t old, sched_priority_t priority);
bool sched_replace_priority_floor_locked(thread_t *thread, sched_priority_t old, sched_priority_t priority);

void sched_init();
void sched_ap_entry();

void sched_queue(thread_t *thread);
bool sched_wakeup(thread_t *thread, int reason);

void sched_stop_current_thread();
void sched_prepare_sleep(bool interruptible);
void sched_target_cpu(struct cpu_t *cpu);
void sched_reschedule_on_cpu(struct cpu_t *cpu, bool target);
void sched_sleep_us(size_t us);
int sched_yield();

void sched_thread_running_callback(thread_t *thread);
void sched_thread_stopping_callback(thread_t *thread, bool sleeping);
void sched_thread_wakeup_callback(thread_t *thread);

thread_t *sched_select_next_thread(void);
void sched_insert_in_cpu_queue(struct cpu_t *cpu, thread_t *thread);
void sched_preempt_cpu(struct cpu_t *cpu);
thread_t *sched_steal_work_from_cpu(struct cpu_t *cpu);
bool sched_idle_steal_work(void);

static inline int sched_thread_run_queue_index(int interactivity) {
	return min(interactivity * SCHED_RUN_QUEUE_SIZE / SCHED_MAX_INTERACTIVITY, SCHED_RUN_QUEUE_SIZE - 1);
}

static inline unsigned sched_priority_queue(sched_priority_t priority) {
	return (SCHED_PRIORITY_MAX - priority) / SCHED_MAX_INTERACTIVITY;
}

static inline unsigned sched_priority_score(sched_priority_t priority) {
	return (SCHED_PRIORITY_MAX - priority) % SCHED_MAX_INTERACTIVITY;
}

static inline bool sched_thread_can_run_in_cpu(thread_t *thread, int cpu_queue, int cpu_interactivity) {
	sched_priority_t priority = sched_thread_priority(thread);
	unsigned queue = sched_priority_queue(priority);
	unsigned score = sched_priority_score(priority);

	// if thread is timeshare, can only immediatelly run when cpu queue is running idle threads,
	if (queue == SCHED_QUEUE_TIMESHARE)
		return cpu_queue == SCHED_QUEUE_IDLE;

	// otherwise, realtime > idle and if a tie the index in the queue
	return queue < cpu_queue || (queue == cpu_queue && sched_thread_run_queue_index(score) < sched_thread_run_queue_index(cpu_interactivity));
}

#endif
