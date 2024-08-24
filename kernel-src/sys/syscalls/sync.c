#include <kernel/syscalls.h>
#include <kernel/vmmcache.h>

syscallret_t syscall_sync(context_t *) {
	syscallret_t ret = {0};
	vmmcache_sync();
	return ret;
}

syscallret_t syscall_fsync(context_t *context, int fd) {
	syscallret_t ret = {
		.ret = -1
	};

	file_t *file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		return ret;
	}

	ret.errno = VOP_SYNC(file->vnode);

	fd_release(file);
	ret.ret = ret.errno ? -1 : 0;
	return ret;
}
