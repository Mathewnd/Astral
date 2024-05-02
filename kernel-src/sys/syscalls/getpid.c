#include <kernel/syscalls.h>
#include <arch/cpu.h>
#include <kernel/jobctl.h>
#include <logging.h>

syscallret_t syscall_getpid(context_t *ctx) {
	syscallret_t ret = {
		.ret = _cpu()->thread->proc->pid,
		.errno = 0
	};
	return ret;
}

syscallret_t syscall_getppid(context_t *ctx) {
	syscallret_t ret = {
		.ret = _cpu()->thread->proc->parent ? _cpu()->thread->proc->parent->pid : 1,
		.errno = 0
	};
	return ret;
}

syscallret_t syscall_getpgid(context_t *ctx, pid_t pid) {
	syscallret_t ret = {
		.errno = 0
	};

	__assert(pid == 0);
	ret.ret = jobctl_getpgid(_cpu()->thread->proc);
	return ret;
}

syscallret_t syscall_getsid(context_t *ctx, pid_t pid) {
	syscallret_t ret = {
		.errno = 0
	};

	__assert(pid == 0);
	ret.ret = jobctl_getsid(_cpu()->thread->proc);
	return ret;
}
