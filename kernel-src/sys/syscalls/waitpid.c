#include <kernel/syscalls.h>
#include <kernel/abi.h>
#include <arch/cpu.h>
#include <errno.h>
#include <logging.h>

syscallret_t syscall_waitpid(context_t *context, pid_t pid, int *status, int options) {
	syscallret_t ret = {
		.ret = -1
	};

	// TODO address check
	// TODO implement these
	__assert(options == 0);
	__assert(pid == -1 || pid > 0);

	thread_t *thread = _cpu()->thread;
	proc_t *proc = thread->proc;
	spinlock_acquire(&proc->lock);

	proc_t *prev = NULL;
	proc_t *iterator = proc->child;
	proc_t *desired = NULL;

	for (;;) {
		if (iterator == NULL) {
			if (pid > 0 && desired == NULL) {
				ret.errno = ECHILD;
				spinlock_release(&proc->lock);
				return ret;
			}
			spinlock_release(&proc->lock);
			if (semaphore_wait(&proc->waitsem, true)) {
				ret.errno = EINTR;
				return ret;
			}
			spinlock_acquire(&proc->lock);
			prev = NULL;
			iterator = proc->child;
			desired = NULL; // set as null to be certain another thread didn't get it
			continue;
		}

		if (pid > 0 && iterator->pid == pid)
			desired = iterator;

		if (iterator->state == SCHED_PROC_STATE_ZOMBIE)
			break;

		prev = iterator;
		iterator = iterator->sibling;
	}

	// iterator has the zombie child

	if (prev)
		prev->sibling = iterator->sibling;
	else
		proc->child = iterator->sibling;

	spinlock_release(&proc->lock);

	// TODO memory safe operation
	// TODO the signal number goes in the low byte of status
	if (status)
		*status = iterator->status << 8;

	for (int i = 0; i < iterator->threadtablesize; ++i) {
		if (iterator->threads[i] == NULL)
			continue;
		sched_destroythread(iterator->threads[i]);
	}

	ret.ret = iterator->pid;
	ret.errno = 0;

	__assert(iterator);
	// process will no longer be referenced by the child list
	PROC_RELEASE(iterator);
	return ret;
}
