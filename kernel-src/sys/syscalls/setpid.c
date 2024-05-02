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
	__assert(pid == 0 || pid == _cpu()->thread->proc->pid);
	__assert(pgid == 0 || pgid == _cpu()->thread->proc->pid);

	ret.errno = jobctl_newgroup(_cpu()->thread->proc);
	ret.ret = ret.errno ? -1 : _cpu()->thread->proc->pid;
	return ret;
}
