#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/vfs.h>
#include <errno.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

syscallret_t syscall_seek(context_t *context, int fd, off_t offset, int whence) {
	syscallret_t ret = {
		.ret = -1,
		.errno = 0
	};

	if (whence > 2) {
		ret.errno = EINVAL;
		return ret;
	}

	file_t *file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		return ret;
	}

	if (file->vnode->type == V_TYPE_SOCKET || file->vnode->type == V_TYPE_FIFO) {
		ret.errno = ESPIPE;
		goto cleanup;
	}

	size_t size = -1;
	uintmax_t curroffset = file->offset;
	uintmax_t newoffset = 0;

	// determine size

	if (file->vnode->type == V_TYPE_CHDEV) {
		ret.errno = VOP_MAXSEEK(file->vnode, &size);
		if (ret.errno) {
			ret.errno = ESPIPE;
			goto cleanup;
		}
	} else {
		vattr_t attr;
		VOP_LOCK(file->vnode);
		ret.errno = VOP_GETATTR(file->vnode, &attr, &current_thread()->proc->cred);
		VOP_UNLOCK(file->vnode);
		if (ret.errno)
			goto cleanup;
		size = attr.size;
	}

	switch (whence) {
		case SEEK_SET:
			newoffset = offset;
			break;
		case SEEK_CUR:
			newoffset = curroffset + offset;
			if (offset > 0 && newoffset < curroffset) {
				ret.errno = EOVERFLOW;
				goto cleanup;
			}
			if (offset < 0 && newoffset > curroffset) {
				ret.errno = EINVAL;
				goto cleanup;
			}
			break;
		case SEEK_END:
			newoffset = size;
			break;
	}

	file->offset = newoffset;
	ret.ret = newoffset;
	ret.errno = 0;

	cleanup:
	fd_release(file);
	return ret;
}
