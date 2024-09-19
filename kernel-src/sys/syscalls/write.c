#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <kernel/file.h>
#include <errno.h>

syscallret_t syscall_write(context_t *context, int fd, void *buffer, size_t size) {
	syscallret_t ret = {
		.ret = -1
	};

	if (IS_USER_ADDRESS(buffer) == false) {
		ret.errno = EFAULT;
		return ret;
	}

	file_t *file = fd_get(fd);

	if (file == NULL || (file->flags & FILE_WRITE) == 0) {
		ret.errno = EBADF;
		goto cleanup;
	}

	if (size == 0) {
		ret.ret = 0;
		ret.errno = 0;
		goto cleanup;
	}

	size_t byteswritten;
	uintmax_t offset = file->offset;

	if (file->flags & O_APPEND) {
		vattr_t attr;
		// TODO racey
		VOP_LOCK(file->vnode);
		ret.errno = VOP_GETATTR(file->vnode, &attr, &current_thread()->proc->cred);
		VOP_UNLOCK(file->vnode);
		if (ret.errno)
			goto cleanup;

		offset = attr.size;
	}

	ret.errno = vfs_write(file->vnode, buffer, size, offset, &byteswritten, fileflagstovnodeflags(file->flags));

	if (ret.errno)
		goto cleanup;

	file->offset = offset + byteswritten;
	ret.ret = byteswritten;
	ret.errno = 0;
cleanup:
	if (file)
		fd_release(file);

	return ret;
}
