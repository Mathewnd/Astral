#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include <kernel/proc.h>
#include <kernel/thread.h>

#define SCHED_WAKEUP_REASON_NORMAL 0
#define SCHED_WAKEUP_REASON_INTERRUPTED -1

#define STACK_TOP (void *)0x0000800000000000
#define INTERP_BASE (void *)0x00000beef0000000

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

#endif
