#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <errno.h>

syscallret_t syscall_dup(context_t *, int oldfd) {
	syscallret_t ret;
	int r = -1;
	ret.errno = fd_dup(oldfd, 0, false, 0, &r);
	ret.ret = r;
	return ret;
}

syscallret_t syscall_dup2(context_t *, int oldfd, int newfd) {
	syscallret_t ret;
	if (oldfd == newfd) {
		ret.errno = 0;
		ret.ret = newfd;
	} else {
		int r = -1;
		ret.errno = fd_dup(oldfd, newfd, true, 0, &r);
		ret.ret = r;
	}

	return ret;
}

syscallret_t syscall_dup3(context_t *, int oldfd, int newfd, int flags) {
	syscallret_t ret;
	if (oldfd == newfd) {
		ret.errno = EINVAL;
		ret.ret = -1;
	} else {
		int r = -1;
		ret.errno = fd_dup(oldfd, newfd, true, flags, &r);
		ret.ret = r;
	}

	return ret;
}
