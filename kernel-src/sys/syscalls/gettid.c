#include <kernel/syscalls.h>
#include <kernel/scheduler.h>

syscallret_t syscall_gettid(context_t *) {
	syscallret_t ret = {
		.errno = 0,
		.ret = _cpu()->thread->tid
	};

	return ret;
}
