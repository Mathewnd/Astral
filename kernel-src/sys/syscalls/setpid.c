#include <kernel/syscalls.h>
#include <kernel/jobctl.h>
#include <logging.h>

syscallret_t syscall_setsid(context_t *) {
	syscallret_t ret;

	ret.errno = jobctl_newsession(current_thread()->proc);
	ret.ret = ret.errno ? -1 : current_thread()->proc->pid;
	return ret;
}

syscallret_t syscall_setpgid(context_t *, pid_t pid, pid_t pgid) {
	syscallret_t ret;

	proc_t *currentproc = current_thread()->proc;

	if (pgid < 0) {
		ret.errno = EINVAL;
		return ret;
	}

	proc_t *proc = NULL;
	if (pid == 0) {
		proc = currentproc;
		PROC_HOLD(proc);
	} else {
		proc = proc_get_from_pid(pid);
	}

	if (proc == NULL) {
		ret.errno = ESRCH;
		return ret;
	}

	if (proc != currentproc && proc->parent != currentproc) {
		ret.errno = ESRCH;
		PROC_RELEASE(proc);
		return ret;
	}

	proc_t *pgrp = NULL;
	if (pgid == 0) {
		pgrp = currentproc;
		PROC_HOLD(pgrp);
	} else {
		pgrp = proc_get_from_pid(pgid);
	}

	if (pgrp == NULL) {
		PROC_RELEASE(proc);
		ret.errno = EPERM;
		return ret;
	}

	if (pgid == pid || pgid == 0) {
		// create new group for process
		ret.errno = jobctl_newgroup(proc);
	} else {
		// switch process group
		ret.errno = jobctl_changegroup(proc, pgrp);
	}

	PROC_RELEASE(pgrp);
	PROC_RELEASE(proc);

	ret.ret = ret.errno ? -1 : 0;
	return ret;
}
