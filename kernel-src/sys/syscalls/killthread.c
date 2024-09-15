#include <kernel/syscalls.h>
#include <arch/cpu.h>
#include <logging.h>
#include <kernel/jobctl.h>
#include <kernel/auth.h>

syscallret_t syscall_killthread(context_t *context, int pid, int tid, int signal) {
	syscallret_t ret = {
		.ret = -1
	};

	if (pid < 1 || tid < 1 || signal < 0 || signal > NSIG) {
		ret.errno = EINVAL;
		return ret;
	}

	proc_t *sender = current_thread()->proc;
	proc_t *target = sched_getprocfrompid(pid);
	if (target == NULL) {
		ret.errno = ESRCH;
		return ret;
	}

	// permission check
	if (signal != SIGCONT || jobctl_getpgid(target) != jobctl_getpgid(sender)) {
		ret.errno = auth_process_check(&sender->cred, AUTH_ACTIONS_PROCESS_SIGNAL, target);
		if (ret.errno)
			goto leave;
	}

	bool ok = false;
	bool intstatus = spinlock_acquireirqclear(&target->threadlistlock);

	thread_t *thread = target->threadlist;
	while (thread) {
		if (thread->tid == tid) {
			signal_signalthread(thread, signal, false);
			ok = true;
			break;
		}

		thread = thread->procnext;
	}

	spinlock_releaseirqrestore(&target->threadlistlock, intstatus);

	if (ok == false) {
		ret.errno = ESRCH;
		ret.ret = -1;
	} else {
		ret.errno = 0;
		ret.ret = 0;
	}

	leave:
	PROC_RELEASE(target);
	return ret;
}
