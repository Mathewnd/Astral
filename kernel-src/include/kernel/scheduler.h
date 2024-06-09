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

struct proc_t;

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
	context_t context;
	extracontext_t extracontext;
	void *kernelstack;
	size_t kernelstacksize;
	vmmcontext_t *vmmctx;
	tid_t tid;
	int flags;
	long priority;
	bool sleepintstatus;
	spinlock_t sleeplock;
	int wakeupreason;
	bool shouldexit;
	void *kernelarg;
	context_t *usercopyctx;
	struct {
		spinlock_t lock;
		stack_t stack;
		sigset_t mask;
		sigset_t pending;
		sigset_t urgent;
		bool stopped;
	} signals;
} thread_t;

typedef struct proc_t {
	mutex_t mutex;
	int refcount;
	int status;
	int state;
	struct proc_t *sibling;
	struct proc_t *parent;
	struct proc_t *child;
	pid_t pid;
	cred_t cred;
	spinlock_t threadlistlock;
	thread_t *threadlist;
	bool nomorethreads;
	size_t runningthreadcount;
	size_t fdcount;
	uintmax_t fdfirst;
	mutex_t fdmutex;
	struct fd_t *fd;
	mode_t umask;
	int flags;
	vnode_t *cwd;
	vnode_t *root;
	spinlock_t nodeslock;
	semaphore_t waitsem;
	spinlock_t exiting;

	spinlock_t jobctllock;
	struct {
		// NULL if this process is leader
		struct proc_t *leader;

		// these are only applicable if the process is leader
		struct proc_t *foreground; // process group leader, NULL if self
		void *controllingtty;
	} session;

	struct {
		// NULL if this process is leader
		struct proc_t *leader;
		// linked list
		struct proc_t *nextmember;
		struct proc_t *previousmember;

		// these are only applicable if the process is leader
		spinlock_t lock; // protects the linked list
	} pgrp;
	struct {
		spinlock_t lock;
		sigaction_t actions[NSIG];
		sigset_t pending;
		bool stopunwaited;
		bool continueunwaited;
	} signals;

	struct {
		mutex_t mutex;
		itimer_t realtime;
		itimer_t virtualtime;
		itimer_t profiling;
	} timer;
} proc_t;

#include <arch/cpu.h>

#define UMASK(mode) ((mode) & ~_cpu()->thread->proc->umask)
#define PROC_HOLD(v) __atomic_add_fetch(&(v)->refcount, 1, __ATOMIC_SEQ_CST)
// the acquire of the pid table mutex is done here to prevent a nasty race condition where sched_inactiveproc could be called multiple times
// due to the pid table itself not holding any references to the process
#define PROC_RELEASE(v) {\
		MUTEX_ACQUIRE(&sched_pidtablemutex, false); \
		if (__atomic_sub_fetch(&(v)->refcount, 1, __ATOMIC_SEQ_CST) == 0) {\
			sched_inactiveproc(v); \
			(v) = NULL; \
		} \
		MUTEX_RELEASE(&sched_pidtablemutex); \
	}

extern proc_t *sched_initproc;
extern mutex_t sched_pidtablemutex;

proc_t *sched_getprocfrompid(int pid);
void sched_init();
void sched_stopotherthreads();
void sched_runinit();
__attribute__((noreturn)) void sched_threadexit();
void sched_queue(thread_t *thread);
void sched_stopcurrentthread();
int sched_yield();
void sched_preparesleep(bool interruptible);
bool sched_wakeup(thread_t *thread, int reason);
int sched_sleep();
proc_t *sched_newproc();
vnode_t *sched_getcwd();
vnode_t *sched_getroot();
void sched_setcwd(vnode_t *);
void sched_setroot(vnode_t *);
thread_t *sched_newthread(void *ip, size_t kstacksize, int priority, proc_t *proc, void *ustack);
void sched_destroyproc(proc_t *);
void sched_destroythread(thread_t *);
void sched_targetcpu(struct cpu_t *cpu);
void sched_sleepus(size_t us);
void sched_apentry();
void sched_inactiveproc(proc_t *proc);
void sched_terminateprogram(int status);

#endif
