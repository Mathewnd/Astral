#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/vfs.h>

syscallret_t syscall_ioctl(context_t *, int fd, unsigned long request, void *arg) {
	syscallret_t ret = {
		.ret = -1
	};

	file_t *file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		return ret;
	}

	int r = 0;
	ret.errno = VOP_IOCTL(file->vnode, request, arg, &r);
	ret.ret = ret.errno ? -1 : r;

	fd_release(file);

	return ret;
}
