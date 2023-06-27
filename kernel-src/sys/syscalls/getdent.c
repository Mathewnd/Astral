#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/vmm.h>
#include <errno.h>
#include <kernel/alloc.h>

syscallret_t syscall_getdents(context_t *, int dirfd, void *ubuffer, size_t readmax) {
	syscallret_t ret = {
		.ret = -1
	};

	if (ubuffer > USERSPACE_END) {
		ret.errno = EFAULT;
		return ret;
	}

	size_t count = readmax / sizeof(dent_t);

	if (count == 0) {
		ret.errno = EINVAL;
		return ret;
	}

	dent_t *buffer = alloc(readmax);
	if (buffer == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	file_t *fd = fd_get(dirfd);
	if (fd == NULL) {
		free(buffer);
		ret.errno = EBADF;
		return ret;
	}

	if (fd->vnode->type != V_TYPE_DIR) {
		ret.errno = ENOTDIR;
		goto cleanup;
	}

	ret.errno = VOP_GETDENTS(fd->vnode, buffer, count, fd->offset, &ret.ret); 

	if (ret.errno)
		goto cleanup;

	fd->offset += ret.ret;

	ret.ret *= sizeof(dent_t);

	// TODO safe memcpy
	memcpy(ubuffer, buffer, ret.ret);

	cleanup:
	free(buffer);
	fd_release(fd);
	return ret;
}
