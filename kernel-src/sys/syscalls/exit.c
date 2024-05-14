#include <kernel/syscalls.h>
#include <kernel/scheduler.h>
#include <kernel/file.h>
#include <kernel/vfs.h>
#include <logging.h>
#include <semaphore.h>

__attribute__((noreturn)) void syscall_exit(context_t *context, int status) {
	thread_t *thread = _cpu()->thread; 
	proc_t *proc = thread->proc;
	if (spinlock_try(&proc->exiting) == false)
		sched_threadexit();

	sched_stopotherthreads();

	proc->status = status << 8;

	sched_threadexit();
}
