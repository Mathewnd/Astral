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

	sched_stopotherthreads();

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
