#include <kernel/syscalls.h>
#include <kernel/scheduler.h>
#include <kernel/cmdline.h>
#include <kernel/alloc.h>

syscallret_t syscall_newthread(context_t *, void *entry, void *stack) {
	syscallret_t ret = {
		.ret = -1
	};

	// TODO perhaps clean up threads here as well
	// if the kernel was specified to have no user threading in the cmdline
	if (cmdline_get("nomultithread")) {
		ret.errno = ENOSYS;
		return ret;
	}

	proc_t *proc = current_thread()->proc;

	thread_t *thread = sched_newthread(entry, 16 * PAGE_SIZE, 1, current_thread()->proc, stack);
	if (thread == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	thread->vmmctx = current_thread()->vmmctx;

	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->threadlistlock);

	bool shouldntstart = proc->nomorethreads;
	if (shouldntstart == false) {
		thread->procnext = proc->threadlist;
		proc->threadlist = thread;
		__atomic_fetch_add(&proc->runningthreadcount, 1, __ATOMIC_SEQ_CST);
	}

	spinlock_release(&proc->threadlistlock);
	interrupt_set(intstatus);

	if (shouldntstart) {
		sched_destroythread(thread);
		ret.errno = EAGAIN;
		return ret;
	}

	ret.errno = 0;
	ret.ret = thread->tid;

	if (shouldntstart == false)
		sched_queue(thread);
	return ret;
}
