#include <kernel/syscalls.h>
#include <arch/cpu.h>
#include <errno.h>

syscallret_t syscall_invalid() {
	syscallret_t ret = {
		.errno = ENOSYS,
		.ret = -1
	};


	printf("Detected invalid system call, raising SIGSYS and returning ENOSYS\n");
	signal_signalthread(current_thread(), SIGSYS, true);

	return ret;
}
