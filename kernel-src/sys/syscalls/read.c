#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <kernel/file.h>
#include <errno.h>

syscallret_t syscall_read(context_t *context, int fd, void *buffer, size_t size) {
	syscallret_t ret = {
		.ret = -1
	};

	void *kernelbuff = vmm_map(NULL, size, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	if (kernelbuff == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	file_t *file = fd_get(fd);

	if (file == NULL || (file->flags & FILE_READ) == 0) {
		ret.errno = EBADF;
		goto cleanup;
	}

	// TODO pass flags
	size_t bytesread;
	ret.errno = vfs_read(file->vnode, kernelbuff, size, file->offset, &bytesread, 0);

	if (ret.errno)
		goto cleanup;

	file->offset += bytesread;
	ret.ret = bytesread;
	ret.errno = 0;

	// TODO safe memcpy
	memcpy(buffer, kernelbuff, bytesread);

cleanup:
	if (file)
		fd_release(file);

	vmm_unmap(kernelbuff, size, 0);
	return ret;
}
