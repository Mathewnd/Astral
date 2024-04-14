#include <kernel/syscalls.h>
#include <kernel/scheduler.h>
#include <kernel/file.h>
#include <kernel/vfs.h>
#include <logging.h>
#include <semaphore.h>

__attribute__((noreturn)) void syscall_exit(context_t *context, int status) {
	thread_t *thread = _cpu()->thread; 
	proc_t *proc = thread->proc;
	__assert(proc != sched_initproc);
	__assert(spinlock_try(&proc->exiting)); // only one thread can exit at the same time

	// stop other threads
	spinlock_acquire(&proc->lock);
	proc->nomorethreads = true;

	for (int i = 0; i < proc->threadtablesize; ++i) {
		if (proc->threads[i] == NULL || proc->threads[i] == thread)
			continue;

		proc->threads[i]->shouldexit = true;
		sched_wakeup(proc->threads[i], SCHED_WAKEUP_REASON_INTERRUPTED);
	}

	spinlock_release(&proc->lock);

	while (__atomic_load_n(&proc->runningthreadcount, __ATOMIC_SEQ_CST) > 1) sched_yield();

	// close fds
	for (int fd = 0; fd < proc->fdcount; ++fd)
		fd_close(fd);

	// zombify the proc
	spinlock_acquire(&proc->lock);

	VOP_RELEASE(proc->root);
	VOP_RELEASE(proc->cwd);

	proc->status = status;
	proc->state = SCHED_PROC_STATE_ZOMBIE;
	proc_t *lastchild = proc->child;

	spinlock_acquire(&sched_initproc->lock);

	bool haszombie = false;

	while (lastchild && lastchild->sibling) {
		if (lastchild->state == SCHED_PROC_STATE_ZOMBIE)
			haszombie = true;

		lastchild->parent = sched_initproc;
		lastchild = lastchild->sibling;
	}

	if (lastchild) {
		lastchild->sibling = sched_initproc->child;
		sched_initproc->child = proc->child;
	}

	spinlock_release(&sched_initproc->lock);
	spinlock_release(&proc->lock);

	semaphore_signal(&proc->parent->waitsem);
	if (haszombie)
		semaphore_signal(&sched_initproc->waitsem);

	sched_threadexit();
}
