#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include <arch/context.h>
#include <kernel/vmm.h>
#include <kernel/abi.h>
#include <kernel/vfs.h>
#include <semaphore.h>

#define SCHED_THREAD_FLAGS_QUEUED 1
#define SCHED_THREAD_FLAGS_RUNNING 2
#define SCHED_THREAD_FLAGS_SLEEP 4
#define SCHED_THREAD_FLAGS_INTERRUPTIBLE 8

#define SCHED_PROC_STATE_NORMAL 0
#define SCHED_PROC_STATE_ZOMBIE 1

#define STACK_TOP (void *)0x0000800000000000
#define INTERP_BASE (void *)0x00000beef0000000

struct proc_t;

typedef struct thread_t {
	void *kernelstacktop;
	struct thread_t *next;
	struct thread_t *prev;
	struct thread_t *sleepnext;
	struct thread_t *sleepprev;
	struct proc_t *proc;
	struct cpu_t *cpu;
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
} thread_t;

typedef struct proc_t {
	spinlock_t lock;
	int status;
	int state;
	struct proc_t *sibling;
	struct proc_t *parent;
	struct proc_t *child;
	pid_t pid;
	cred_t cred;
	thread_t **threads;
	size_t threadtablesize;
	size_t runningthreadcount;
	size_t fdcount;
	uintmax_t fdfirst;
	spinlock_t fdlock;
	struct fd_t *fd;
	mode_t umask;
	int flags;
	vnode_t *cwd;
	vnode_t *root;
	spinlock_t nodeslock;
	semaphore_t waitsem;
} proc_t;

#include <arch/cpu.h>

extern proc_t *sched_initproc;

void sched_init();
void sched_runinit();
__attribute__((noreturn)) void sched_threadexit();
void sched_queue(thread_t *thread);
void sched_stopcurrentthread();
int sched_yield();
void sched_preparesleep(bool interruptible);
void sched_wakeup(thread_t *thread, int reason);
int sched_sleep();
proc_t *sched_newproc();
vnode_t *sched_getcwd();
vnode_t *sched_getroot();
void sched_setcwd(vnode_t *);
void sched_setroot(vnode_t *);
thread_t *sched_newthread(void *ip, size_t kstacksize, int priority, proc_t *proc, void *ustack);
void sched_destroyproc(proc_t *);
void sched_destroythread(thread_t *);

#endif
