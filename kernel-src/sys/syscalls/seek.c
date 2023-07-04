#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/vfs.h>
#include <errno.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

syscallret_t syscall_seek(context_t *context, int fd, off_t offset, int whence) {
	syscallret_t ret = {
		.ret = -1
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

	// TODO device check seek

	uintmax_t curroffset = file->offset;
	uintmax_t newoffset = 0;

	switch (whence) {
		case SEEK_SET:
		newoffset = offset;
		break;
		case SEEK_CUR: {
			uintmax_t newoffset = curroffset + offset;
			if (offset > 0 && newoffset < curroffset) {
				ret.errno = EOVERFLOW;
				goto cleanup;
			}
			if (offset < 0 && newoffset > curroffset) {
				ret.errno = EINVAL;
				goto cleanup;
			}
			newoffset = newoffset;
			break;
		}
		case SEEK_END: {
			vattr_t attr;
			ret.errno = VOP_GETATTR(file->vnode, &attr, &_cpu()->thread->proc->cred);
			if (ret.errno) 
				goto cleanup;
			newoffset = attr.size;
			break;
		}
	}

	file->offset = newoffset;
	ret.ret = file->offset;
	ret.errno = 0;

	cleanup:
	fd_release(file);
	return ret;
}
