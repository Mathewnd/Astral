#include <kernel/syscalls.h>
#include <arch/cpu.h>
#include <logging.h>
#include <kernel/jobctl.h>
#include <kernel/auth.h>

syscallret_t syscall_kill(context_t *context, int pid, int signal) {
	syscallret_t ret = {
		.ret = -1
	};

	int effectivepid = -1;

	if (pid > 0) // specific process
		effectivepid = pid;
	else if (pid == 0) // own process group
		effectivepid = 0;
	else if (pid == -1) // every process, unsupported atm
		effectivepid = -1;
	else if (pid < -1)
		effectivepid = -pid; // target specific process group

	proc_t *target = sched_getprocfrompid(effectivepid);
	if (target == NULL && pid != 0 && pid != -1) {
		ret.errno = ESRCH;
		return ret;
	}

	// null signal is meant to find out about the existance of a process
	if (signal == 0)
		goto leave;

	if (pid >= 1) {
		if (signal != SIGCONT && jobctl_getpgid(target) != jobctl_getpgid(_cpu()->thread->proc)) {
			ret.errno = auth_process_check(&_cpu()->thread->proc->cred, AUTH_ACTIONS_PROCESS_SIGNAL, target);
			if (ret.errno)
				goto leave;
		}

		signal_signalproc(target, signal);
	} else if (pid == -1) {
		ret.errno = sched_signalall(signal, _cpu()->thread->proc);
	} else {
		ret.errno = jobctl_signal(target, signal, _cpu()->thread->proc);
	}

	leave:
	if (target) {
		PROC_RELEASE(target);
	}

	ret.ret = ret.errno ? -1 : 0;
	return ret;
}
