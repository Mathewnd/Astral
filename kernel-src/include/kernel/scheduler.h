#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include <kernel/proc.h>
#include <kernel/thread.h>
#include <bitmap.h>

#define SCHED_WAKEUP_REASON_NORMAL 0
#define SCHED_WAKEUP_REASON_INTERRUPTED -1

#define STACK_TOP (void *)0x0000800000000000
#define INTERP_BASE (void *)0x00000beef0000000

#define SCHED_RUN_QUEUE_SIZE 32
#define SCHED_MAX_INTERACTIVITY 100

typedef struct {
	 struct {
		 thread_t *ins;
		 thread_t *run;
	 } queues[SCHED_RUN_QUEUE_SIZE];
	bitmap_t thread_bitmap;
} sched_run_queue_t;

typedef struct {
	thread_t *queues[SCHED_RUN_QUEUE_SIZE];
	int ins;
	int run;
	size_t thread_count;
} sched_calendar_queue_t;

extern bitmap_t sched_idle_cpu_bitmap;
extern spinlock_t sched_idle_cpu_bitmap_lock;

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

#endif
