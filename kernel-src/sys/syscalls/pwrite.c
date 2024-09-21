#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <kernel/file.h>
#include <errno.h>

syscallret_t syscall_pwrite(context_t *context, int fd, void *buffer, size_t size, uintmax_t offset) {
	syscallret_t ret = {
		.ret = -1
	};

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

	size_t tmp;
	if (file->vnode->type == V_TYPE_SOCKET || file->vnode->type == V_TYPE_FIFO || (file->vnode->type == V_TYPE_CHDEV && VOP_MAXSEEK(file->vnode, &tmp))) {
		ret.errno = ESPIPE;
		goto cleanup;
	}

	size_t byteswritten;
	ret.errno = vfs_write(file->vnode, buffer, size, offset, &byteswritten, fileflagstovnodeflags(file->flags));

	if (ret.errno)
		goto cleanup;

	ret.ret = byteswritten;
	ret.errno = 0;
cleanup:
	if (file)
		fd_release(file);

	return ret;
}
