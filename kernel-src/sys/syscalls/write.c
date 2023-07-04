#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <kernel/file.h>
#include <errno.h>

syscallret_t syscall_write(context_t *context, int fd, void *buffer, size_t size) {
	syscallret_t ret = {
		.ret = -1
	};

	void *kernelbuff = vmm_map(NULL, size, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	if (kernelbuff == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	file_t *file = fd_get(fd);

	if (file == NULL || (file->flags & FILE_WRITE) == 0) {
		ret.errno = EBADF;
		goto cleanup;
	}

	// TODO safe memcpy
	memcpy(kernelbuff, buffer, size);

	// TODO pass flags
	size_t byteswritten;
	uintmax_t offset = file->offset;
	ret.errno = vfs_write(file->vnode, kernelbuff, size, offset, &byteswritten, 0);

	if (ret.errno)
		goto cleanup;

	file->offset = offset + byteswritten;
	ret.ret = byteswritten;
	ret.errno = 0;
cleanup:
	if (file)
		fd_release(file);

	vmm_unmap(kernelbuff, size, 0);
	return ret;
}
