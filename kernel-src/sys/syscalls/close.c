#include <kernel/syscalls.h>
#include <kernel/file.h>

syscallret_t syscall_close(context_t *context, int fd) {
	syscallret_t ret = {
		.ret = -1
	};

	ret.errno = fd_close(fd);
	ret.ret = 0;

	return ret;
}
