#include <kernel/syscalls.h>
#include <arch/cpu.h>

syscallret_t syscall_umask(context_t *, mode_t mode) {
	syscallret_t ret = {
		.errno = 0
	};

	mode &= 0777;

	ret.ret = __atomic_exchange_n(&_cpu()->thread->proc->umask, mode, __ATOMIC_SEQ_CST);

	return ret;
}
