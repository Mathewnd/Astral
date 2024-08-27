#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <kernel/file.h>

syscallret_t syscall_isatty(context_t *context, int fd) {
	syscallret_t ret = {
		.ret = -1
	};

	file_t *file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		return ret;
	}

	// mlibc expects isatty to return 1 if a terminal and 0 if not.
	VOP_LOCK(file->vnode);
	int e = VOP_ISATTY(file->vnode);
	VOP_UNLOCK(file->vnode);

	ret.errno = e;
	ret.ret = e ? 0 : 1;

	fd_release(file);

	return ret;
}
