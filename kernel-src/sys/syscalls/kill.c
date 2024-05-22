#include <kernel/syscalls.h>
#include <arch/cpu.h>
#include <logging.h>
#include <kernel/jobctl.h>

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
	
	if (effectivepid == -1) {
		ret.errno = ESRCH;
		return ret;
	}

	proc_t *target = sched_getprocfrompid(effectivepid);
	if (target == NULL && pid != 0) {
		ret.errno = ESRCH;
		return ret;
	}

	if (pid >= 1)
		signal_signalproc(target, signal);
	else
		ret.errno = jobctl_signal(target, signal);

	if (target) {
		PROC_RELEASE(target);
	}

	ret.errno = 0;
	ret.ret = 0;
	return ret;
}
