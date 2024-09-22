#ifndef _PROC_H
#define _PROC_H

#include <arch/context.h>
#include <kernel/vmm.h>
#include <kernel/abi.h>
#include <kernel/vfs.h>
#include <semaphore.h>
#include <mutex.h>
#include <kernel/signal.h>
#include <kernel/itimer.h>
#include <kernel/event.h>

#include <kernel/thread.h>

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

#define UMASK(mode) ((mode) & ~current_thread()->proc->umask)
#define PROC_HOLD(v) __atomic_add_fetch(&(v)->refcount, 1, __ATOMIC_SEQ_CST)
// the acquire of the pid table mutex is done here to prevent a nasty race condition where sched_inactiveproc could be called multiple times
// due to the pid table itself not holding any references to the process
#define PROC_RELEASE(v) {\
		MUTEX_ACQUIRE(&proc_pid_table_mutex, false); \
		if (__atomic_sub_fetch(&(v)->refcount, 1, __ATOMIC_SEQ_CST) == 0) {\
			proc_inactive(v); \
			(v) = NULL; \
		} \
		MUTEX_RELEASE(&proc_pid_table_mutex); \
	}

extern proc_t *init_proc;
extern mutex_t proc_pid_table_mutex;

proc_t *proc_get_from_pid(int pid);
proc_t *proc_create(void);
vnode_t *proc_get_cwd(void);
vnode_t *proc_get_root(void);
void proc_set_cwd(vnode_t *);
void proc_set_root(vnode_t *);
void proc_destroy(proc_t *);
void proc_inactive(proc_t *proc);
void proc_terminate(int status);
int proc_signal_all(int signal, proc_t *sender);
void proc_stop_other_threads(void);
pid_t proc_allocate_pid(void);
void proc_init(void);

#endif
