#include <kernel/syscalls.h>
#include <kernel/abi.h>
#include <arch/cpu.h>
#include <errno.h>
#include <logging.h>

#define WNOHANG 1
#define WSTOPPED 2
#define WCONTINUED 8
#define KNOWN_FLAGS (WNOHANG | WSTOPPED | WCONTINUED)

syscallret_t syscall_waitpid(context_t *context, pid_t pid, int *status, int options) {
	syscallret_t ret = {
		.ret = -1
	};

	// TODO address check
	// TODO implement these
	__assert((options & ~KNOWN_FLAGS) == 0);
	__assert(pid == -1 || pid > 0);

	thread_t *thread = _cpu()->thread;
	proc_t *proc = thread->proc;
	MUTEX_ACQUIRE(&proc->mutex, false);

	proc_t *prev = NULL;
	proc_t *iterator = proc->child;
	proc_t *desired = NULL;

	if (iterator == NULL) {
		ret.errno = ECHILD;
		MUTEX_RELEASE(&proc->mutex);
		return ret;
	}

	for (;;) {
		if (iterator == NULL) {
			if (pid > 0 && desired == NULL) {
				ret.errno = ECHILD;
				MUTEX_RELEASE(&proc->mutex);
				return ret;
			}
			MUTEX_RELEASE(&proc->mutex);
			if ((options & WNOHANG) && semaphore_test(&proc->waitsem) == false) {
				ret.errno = 0;
				ret.ret = 0;
				return ret;
			} else if ((options & WNOHANG) == 0 && semaphore_wait(&proc->waitsem, true)) {
				ret.errno = EINTR;
				return ret;
			}
			MUTEX_ACQUIRE(&proc->mutex, false);
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

	MUTEX_RELEASE(&proc->mutex);

	// TODO memory safe operation
	if (status) {
		*status = iterator->status;
	}

	for (int i = 0; i < iterator->threadtablesize; ++i) {
		if (iterator->threads[i] == NULL)
			continue;

		volatile int *flags = (volatile int *)(&iterator->threads[i]->flags);
		// wait until the thread can actually be unallocated
		while ((*flags & SCHED_THREAD_FLAGS_DEAD) == 0)
			sched_yield();

		sched_destroythread(iterator->threads[i]);
	}

	ret.ret = iterator->pid;
	ret.errno = 0;

	__assert(iterator);
	// process will no longer be referenced by the child list
	PROC_RELEASE(iterator);
	return ret;
}
