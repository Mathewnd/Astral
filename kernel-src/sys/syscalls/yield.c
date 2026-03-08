#include <kernel/syscalls.h>
#include <kernel/scheduler.h>

syscallret_t syscall_yield(context_t *) {
	syscallret_t ret = {0};

	sched_yield();

	return ret;
}
