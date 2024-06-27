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

	__assert((options & ~KNOWN_FLAGS) == 0);
	// TODO implement these
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

	bool continued = false;
	bool stopped = false;
	bool zombie = false;

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

		if ((pid > 0 && iterator->pid == pid) || pid == -1) {
			desired = iterator;
			bool intstatus = interrupt_set(false);
			spinlock_acquire(&iterator->signals.lock);
			if (iterator->signals.continueunwaited && (options & WCONTINUED)) {
				iterator->signals.continueunwaited = false;
				continued = true;
			}
			if (iterator->signals.stopunwaited && (options & WSTOPPED)) {
				iterator->signals.stopunwaited = false;
				stopped = true;
			}
			spinlock_release(&iterator->signals.lock);
			interrupt_set(intstatus);

			zombie = iterator->state == SCHED_PROC_STATE_ZOMBIE;
		}

		if (zombie || continued || stopped)
			break;

		prev = iterator;
		iterator = iterator->sibling;
	}

	// iterator has the waited child
	if (zombie) {
		// remove process from child list if zombie
		if (prev)
			prev->sibling = iterator->sibling;
		else
			proc->child = iterator->sibling;
	}

	if (status) {
		int statustmp = iterator->status;
		ret.errno = usercopy_touser(status, &statustmp, sizeof(statustmp));
	} else {
		ret.errno = 0;
	}

	MUTEX_RELEASE(&proc->mutex);

	if (zombie) {
		bool intstatus = interrupt_set(false);
		spinlock_acquire(&iterator->threadlistlock);

		thread_t *threadlist = iterator->threadlist;
		iterator->threadlist = NULL;

		spinlock_release(&iterator->threadlistlock);
		interrupt_set(intstatus);

		while (threadlist) {
			thread_t *freethread = threadlist;
			threadlist = threadlist->procnext;
			volatile int *flags = (volatile int *)(&freethread->flags);
			// wait until the thread can actually be unallocated
			while ((*flags & SCHED_THREAD_FLAGS_DEAD) == 0)
				sched_yield();

			sched_destroythread(freethread);
		}
	}

	ret.ret = iterator->pid;

	__assert(iterator);
	// process will no longer be referenced by the child list
	if (zombie) {
		PROC_RELEASE(iterator);
	}

	return ret;
}
