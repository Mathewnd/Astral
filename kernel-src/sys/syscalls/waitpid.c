#include <kernel/syscalls.h>
#include <kernel/abi.h>
#include <arch/cpu.h>
#include <errno.h>
#include <logging.h>
#include <kernel/jobctl.h>

#define __WALL 0x40000000
#define WNOHANG 1
#define WSTOPPED 2
#define WCONTINUED 8
#define KNOWN_FLAGS (WNOHANG | WSTOPPED | WCONTINUED | __WALL)

syscallret_t syscall_waitpid(context_t *context, pid_t pid, int *status, int options) {
	syscallret_t ret = {
		.ret = -1
	};

	bool check_pgid = false;

	if (options & ~KNOWN_FLAGS)
		printf("waitpid: unknown %x\n", options);

	if (pid < -1 || pid == 0) {
		check_pgid = true;
		pid = -pid;
	}

	thread_t *thread = current_thread();
	proc_t *proc = thread->proc;
	MUTEX_ACQUIRE(&proc->mutex);

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

			if (options & WNOHANG) {
				MUTEX_RELEASE(&proc->mutex);
				ret.errno = 0;
				ret.ret = 0;
				return ret;
			}

			eventlistener_t listener;
			EVENT_INITLISTENER(&listener);
			EVENT_ATTACH(&listener, &proc->child_exit_event);
			MUTEX_RELEASE(&proc->mutex);

			ret.errno = EVENT_WAIT(&listener, 0);
			EVENT_DETACHALL(&listener);
			if (ret.errno)
				return ret;

			MUTEX_ACQUIRE(&proc->mutex);
			prev = NULL;
			iterator = proc->child;
			desired = NULL; // set as null to be certain another thread didn't get it
			continue;
		}

		if ((pid > 0 && iterator->pid == pid) || 
				pid == -1 ||
				(check_pgid && ((pid == 0 && 
				 jobctl_getpgid(proc) == jobctl_getpgid(iterator)) ||
				jobctl_getpgid(iterator) == pid))) {
			desired = iterator;
			if (iterator->signals.continueunwaited && (options & WCONTINUED)) {
				iterator->signals.continueunwaited = false;
				continued = true;
			}
			if (iterator->signals.stopunwaited && (options & WSTOPPED)) {
				iterator->signals.stopunwaited = false;
				stopped = true;
			}

			zombie = iterator->state == PROC_STATE_ZOMBIE;
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
			// wait until the thread can actually be unallocated
			for (;;) {
				eventlistener_t listener;
				EVENT_INITLISTENER(&listener);
				EVENT_ATTACH(&listener, &iterator->thread_exit_event);
				if (freethread->flags & THREAD_FLAGS_DEAD) {
					EVENT_DETACHALL(&listener);
					break;
				}

				EVENT_WAIT(&listener, 0);
				EVENT_DETACHALL(&listener);
			}

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
