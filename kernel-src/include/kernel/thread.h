#ifndef _THREAD_H
#define _THREAD_H

#include <stdint.h>
#include <arch/context.h>
#include <kernel/mm.h>
#include <kernel/abi.h>
#include <kernel/signal.h>
#include <kernel/event.h>
#include <kernel/piab.h>
#include <kernel/kcov.h>
#include <list.h>

#define THREAD_FLAGS_QUEUED 1
#define THREAD_FLAGS_RUNNING 2
#define THREAD_FLAGS_SLEEP 4
#define THREAD_FLAGS_INTERRUPTIBLE 8
#define THREAD_FLAGS_PREEMPTED 16
#define THREAD_FLAGS_DEAD 32
// sched_yield queues the thread if a wake arrives here
#define THREAD_FLAGS_SLEEP_PREPARING 64

struct proc_t;

// Scores range from 0 to SCHED_MAX_INTERACTIVITY - 1
#define SCHED_MAX_INTERACTIVITY 100
#define SCHED_PRIORITY_MAX (3 * SCHED_MAX_INTERACTIVITY)

// Higher values select higher priority across the scheduler queues
typedef uint16_t sched_priority_t;

_Static_assert(SCHED_PRIORITY_MAX <= UINT16_MAX,
	"SCHED_PRIORITY_MAX doesn't fit sched_priority_t");

#define THREAD_CLASS_TIMESHARE 0
#define THREAD_CLASS_REAL_TIME 1
#define THREAD_CLASS_IDLE 2

typedef struct thread_t {
	void *kernelstacktop;
	list_node_t queue_node;
	struct thread_t *sleepnext;
	struct thread_t *sleepprev;
	struct thread_t *procnext;
	struct proc_t *proc;
	struct cpu_t *cpu;
	struct cpu_t *cputarget;
	struct cpu_t *last_cpu;
	sched_priority_t queued_priority;
	unsigned queue_index;
#ifdef KCOV_ENABLED
	kcov_t *kcov;
#endif
	context_t context;
	extracontext_t extracontext;
	void *kernelstack;
	size_t kernelstacksize;
	mm_context_t *mmctx;
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
	spinlock_t priority_lock;
	sched_priority_t base_priority;
	sched_priority_t priority_floor;
	uint16_t priority_floor_counts[SCHED_PRIORITY_MAX + 1];
	struct piab_thread_state *piab_state;
	bool piab_tracking_exhausted;
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
