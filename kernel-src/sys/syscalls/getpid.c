#include <kernel/syscalls.h>
#include <arch/cpu.h>

syscallret_t syscall_getpid(context_t *ctx) {
	syscallret_t ret = {
		.ret = _cpu()->thread->proc->pid,
		.errno = 0
	};
	return ret;
}
