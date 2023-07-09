#include <kernel/syscalls.h>

syscallret_t syscall_ftruncate(int fd, size_t size) {
	syscallret_t ret = {
		.ret = -1
	};

	file_t *file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		return ret;
	}

	if ((file->flags & FILE_WRITE) == 0 || file->vnode->type != V_TYPE_REGULAR) {
		ret.errno = EINVAL;
		goto cleanup;
	}

	ret.errno = VOP_RESIZE(file->vnode, size, &_cpu()->thread->proc->cred);
	ret.ret = ret.errno ? -1 : 0;

	cleanup:

	fd_release(file);

	return ret;	
}
