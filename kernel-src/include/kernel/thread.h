#ifndef _THREAD_H
#define _THREAD_H

#include <arch/context.h>
#include <kernel/vmm.h>
#include <kernel/abi.h>
#include <kernel/signal.h>
#include <kernel/event.h>

#define THREAD_FLAGS_QUEUED 1
#define THREAD_FLAGS_RUNNING 2
#define THREAD_FLAGS_SLEEP 4
#define THREAD_FLAGS_INTERRUPTIBLE 8
#define THREAD_FLAGS_PREEMPTED 16
#define THREAD_FLAGS_DEAD 32

struct proc_t;

#define THREAD_CLASS_TIMESHARE 0
#define THREAD_CLASS_REAL_TIME 1
#define THREAD_CLASS_IDLE 2

typedef struct thread_t {
	void *kernelstacktop;
	struct thread_t *next;
	struct thread_t *prev;
	struct thread_t *sleepnext;
	struct thread_t *sleepprev;
	struct thread_t *procnext;
	struct proc_t *proc;
	struct cpu_t *cpu;
	struct cpu_t *cputarget;
	struct cpu_t *last_cpu;
	context_t context;
	extracontext_t extracontext;
	void *kernelstack;
	size_t kernelstacksize;
	vmmcontext_t *vmmctx;
	tid_t tid;
	int flags;
	bool sleepintstatus;
	spinlock_t sleeplock;
	int wakeupreason;
	bool shouldexit;
	void *kernelarg;
	context_t *usercopyctx;
	int class;
	int nice;
	struct {
		spinlock_t lock;
		eventheader_t waitpendingevent;
		stack_t stack;
		sigset_t mask;
		sigset_t pending;
		sigset_t urgent; // always handled before returning to userspace
		sigset_t waiting; // for signal_wait
		sigset_t returnmask; // for signal_returnmask
		bool hasreturnmask;
		bool stopped;
	} signals;
	struct {
		int interactivity_score;
		time_t sleep_time_avg_us;
		time_t run_time_avg_us;
		timespec_t sleep_start;
		timespec_t run_start;
		time_t last_sleep_duration_us;
	} metrics;
} thread_t;

__attribute__((noreturn)) void sched_threadexit();
thread_t *sched_newthread(void *ip, size_t kstacksize, int nice, struct proc_t *proc, void *ustack);
void sched_destroythread(thread_t *);

#endif
