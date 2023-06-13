#include <kernel/syscalls.h>
#include <kernel/vmm.h>
#include <errno.h>

syscallret_t syscall_munmap(context_t *ctx, void *addr, size_t length) {
	syscallret_t ret = {
		.ret = -1,
		.errno = 0
	};

	if (addr > USERSPACE_END) {
		ret.errno = EFAULT;
		return ret;
	}

	if (length == 0 || length % PAGE_SIZE > 0) {
		ret.errno = EINVAL;
		return ret;
	}

	vmm_unmap(addr, length / PAGE_SIZE, 0);

	ret.ret = 0;
	ret.errno = 0;

	return ret;
}
