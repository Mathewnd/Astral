#include <kernel/syscalls.h>

syscallret_t syscall_pause(context_t *) {
	syscallret_t ret = {
		.errno = EINTR,
		.ret = -1
	};

	sched_prepare_sleep(true);
	sched_yield();

	return ret;
}
