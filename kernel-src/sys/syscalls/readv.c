#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <kernel/file.h>
#include <arch/cpu.h>
#include <errno.h>
#include <logging.h>
#include <kernel/alloc.h>

syscallret_t syscall_readv(context_t *context, int fd, iovec_t *uiov, int iovec_count) {
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
	if (file == NULL || (file->flags & FILE_READ) == 0) {
		ret.errno = EBADF;
		goto cleanup;
	}

	iovec_iterator_t iovec_iterator;
	iovec_iterator_init(&iovec_iterator, iovec, iovec_count);

	size_t bytes_read;
	uintmax_t offset = file->offset;
	ret.errno = vfs_read_iovec(file->vnode, &iovec_iterator, buffer_size, offset, &bytes_read, fileflagstovnodeflags(file->flags));

	if (ret.errno)
		goto cleanup;

	file->offset = offset + bytes_read;
	ret.ret = bytes_read;

cleanup:
	if (file)
		fd_release(file);

	free(iovec);

	return ret;
}
