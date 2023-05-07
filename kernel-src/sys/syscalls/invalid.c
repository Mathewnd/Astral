#include <kernel/syscalls.h>
#include <errno.h>

syscallret_t syscall_invalid() {
	syscallret_t ret = {
		.errno = ENOSYS,
		.ret = -1
	};
	return ret;
}
