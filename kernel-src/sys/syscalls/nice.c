#include <kernel/syscalls.h>
#include <kernel/thread.h>
#include <kernel/auth.h>
#include <arch/cpu.h>

syscallret_t syscall_nice(context_t *, int nice) {
	syscallret_t ret;

	ret.errno = nice < 0 ? auth_process_check(&current_thread()->proc->cred, AUTH_ACTIONS_PROCESS_NICE, NULL) : 0;
	current_thread()->proc->nice = max(min(current_thread()->proc->nice + nice, SCHED_NICE_MAX), SCHED_NICE_MIN);

	ret.ret = current_thread()->proc->nice;
	return ret;
}
