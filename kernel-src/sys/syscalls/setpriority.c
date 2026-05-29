#include <kernel/syscalls.h>
#include <kernel/thread.h>
#include <kernel/auth.h>
#include <arch/cpu.h>

syscallret_t syscall_setpriority(context_t *, int which, int who, int priority) {
	syscallret_t ret = {
		.ret = -1
	};

	if (which != 0) {
		ret.errno = EINVAL;
		return ret;
	}

	if (who) {
		ret.errno = ESRCH;
		return ret;
	}

	ret.errno = priority < current_thread()->proc->nice ? auth_process_check(&current_thread()->proc->cred, AUTH_ACTIONS_PROCESS_NICE, NULL) : 0;
	if (ret.errno)
		return ret;

	current_thread()->proc->nice = max(min(priority, SCHED_NICE_MAX), SCHED_NICE_MIN);

	ret.ret = 0;
	return ret;
}
