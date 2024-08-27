#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <kernel/file.h>
#include <arch/cpu.h>
#include <errno.h>
#include <logging.h>

syscallret_t syscall_pread(context_t *context, int fd, void *buffer, size_t size, uintmax_t offset) {
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
	
	size_t bytesread;
	ret.errno = vfs_read(file->vnode, kernelbuff, size, offset, &bytesread, fileflagstovnodeflags(file->flags));

	if (ret.errno)
		goto cleanup;

	ret.errno = usercopy_touser(buffer, kernelbuff, bytesread);
	ret.ret = ret.errno ? -1 : bytesread;
cleanup:
	if (file)
		fd_release(file);

	vmm_unmap(kernelbuff, size, 0);
	return ret;
}
