#include <kernel/syscalls.h>
#include <kernel/scheduler.h>

syscallret_t syscall_threadexit(context_t *) {
	sched_threadexit();
}
