#ifndef _THREAD_H
#define _THREAD_H

#include <arch/context.h>
#include <kernel/vmm.h>
#include <kernel/abi.h>
#include <kernel/signal.h>

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
} thread_t;

#endif
