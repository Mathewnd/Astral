#include <kernel/syscalls.h>
#include <kernel/scheduler.h>
#include <kernel/file.h>
#include <kernel/vfs.h>
#include <logging.h>
#include <semaphore.h>

__attribute__((noreturn)) void syscall_exit(context_t *context, int status) {
	proc_terminate(status << 8);
	__builtin_unreachable();
}
