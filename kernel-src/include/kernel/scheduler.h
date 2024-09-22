#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include <arch/context.h>
#include <kernel/vmm.h>
#include <kernel/abi.h>
#include <kernel/vfs.h>
#include <semaphore.h>
#include <mutex.h>
#include <kernel/signal.h>
#include <kernel/itimer.h>
#include <kernel/event.h>
#include <kernel/proc.h>

#define SCHED_THREAD_FLAGS_QUEUED 1
#define SCHED_THREAD_FLAGS_RUNNING 2
#define SCHED_THREAD_FLAGS_SLEEP 4
#define SCHED_THREAD_FLAGS_INTERRUPTIBLE 8
#define SCHED_THREAD_FLAGS_PREEMPTED 16
#define SCHED_THREAD_FLAGS_DEAD 32

#define SCHED_PROC_STATE_NORMAL 0
#define SCHED_PROC_STATE_ZOMBIE 1

#define SCHED_WAKEUP_REASON_NORMAL 0
#define SCHED_WAKEUP_REASON_INTERRUPTED -1

#define STACK_TOP (void *)0x0000800000000000
#define INTERP_BASE (void *)0x00000beef0000000

void sched_init();
void sched_runinit();
__attribute__((noreturn)) void sched_threadexit();
void sched_queue(thread_t *thread);
void sched_stopcurrentthread();
int sched_yield();
void sched_preparesleep(bool interruptible);
bool sched_wakeup(thread_t *thread, int reason);
thread_t *sched_newthread(void *ip, size_t kstacksize, int priority, proc_t *proc, void *ustack);
void sched_destroythread(thread_t *);
void sched_targetcpu(struct cpu_t *cpu);
void sched_reschedule_on_cpu(struct cpu_t *cpu, bool target);
void sched_sleepus(size_t us);
void sched_apentry();

#endif
