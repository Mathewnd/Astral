#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <kernel/file.h>
#include <arch/cpu.h>
#include <errno.h>
#include <logging.h>
#include <kernel/alloc.h>

syscallret_t syscall_writev(context_t *context, int fd, iovec_t *uiov, int iovec_count) {
	syscallret_t ret = {
		.ret = -1
	};
	file_t *file = NULL;

	iovec_t *iovec = alloc(sizeof(iovec_t) * iovec_count);
	if (iovec == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	ret.errno = usercopy_fromuser(iovec, uiov, sizeof(iovec_t) * iovec_count);
	if (ret.errno)
		goto cleanup;

	size_t buffer_size = iovec_size(iovec, iovec_count);
	if (buffer_size == 0) {
		ret.errno = 0;
		ret.ret = 0;
		goto cleanup;
	}

	if (iovec_user_check(iovec, iovec_count) == false) {
		ret.errno = EFAULT;
		goto cleanup;
	}

	file = fd_get(fd);
	if (file == NULL || (file->flags & FILE_WRITE) == 0) {
		ret.errno = EBADF;
		goto cleanup;
	}

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

	iovec_iterator_t iovec_iterator;
	iovec_iterator_init(&iovec_iterator, iovec, iovec_count);

	size_t bytes_written;
	ret.errno = vfs_write_iovec(file->vnode, &iovec_iterator, buffer_size, offset, &bytes_written, fileflagstovnodeflags(file->flags));

	if (ret.errno)
		goto cleanup;

	file->offset = offset + bytes_written;
	ret.ret = bytes_written;

cleanup:
	if (file)
		fd_release(file);

	free(iovec);

	return ret;
}
