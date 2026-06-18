#include <kernel/syscalls.h>
#include <kernel/eventfs.h>

#define EFD_CLOEXEC O_CLOEXEC
#define EFD_NONBLOCK O_NONBLOCK
#define EFD_SEMAPHORE 1

#define KNOWN_FLAGS (EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE)

syscallret_t syscall_eventfd(context_t *, size_t initval, int flags) {
	syscallret_t ret = {
		.ret = -1
	};

	if (flags & ~KNOWN_FLAGS) {
		ret.errno = EINVAL;
		return ret;
	}

	vnode_t *vnode;
	ret.errno = eventfs_create_node(&vnode, initval, flags & EFD_SEMAPHORE);
	if (ret.errno)
		return ret;

	file_t *file;
	int fd;
	ret.errno = fd_new((flags & EFD_CLOEXEC) ? O_CLOEXEC : 0, &file, &fd);
	if (ret.errno) {
		VOP_RELEASE(vnode);
		return ret;
	}

	file->vnode = vnode;
	file->flags = FILE_WRITE | FILE_READ | ((flags & EFD_NONBLOCK) ? O_NONBLOCK : 0);
	file->offset = 0;
	file->mode = 0666;

	ret.ret = fd;
	return ret;
}
