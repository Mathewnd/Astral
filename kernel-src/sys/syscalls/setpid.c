#include <kernel/syscalls.h>
#include <kernel/jobctl.h>
#include <logging.h>

syscallret_t syscall_setsid(context_t *) {
	syscallret_t ret;

	ret.errno = jobctl_newsession(_cpu()->thread->proc);
	ret.ret = ret.errno ? -1 : _cpu()->thread->proc->pid;
	return ret;
}

syscallret_t syscall_setpgid(context_t *, pid_t pid, pid_t pgid) {
	syscallret_t ret;

	proc_t *proc = NULL;
	if (pid == 0) {
		proc = _cpu()->thread->proc;
		PROC_HOLD(proc);
	} else {
		proc = sched_getprocfrompid(pid);
	}

	if (proc == NULL) {
		ret.errno = ESRCH;
		return ret;
	}

	proc_t *pgrp = NULL;
	if (pgid == 0) {
		pgrp = _cpu()->thread->proc;
		PROC_HOLD(pgrp);
	} else {
		pgrp = sched_getprocfrompid(pgid);
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
