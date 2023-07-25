#include <kernel/syscalls.h>
#include <kernel/abi.h>
#include <errno.h>
#include <kernel/vmm.h>
#include <logging.h>

#define PROT_READ  0x01
#define PROT_WRITE 0x02
#define PROT_EXEC  0x04

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANON      0x20
#define MAP_ANONYMOUS MAP_ANON

#define KNOWN_PROT (PROT_READ | PROT_WRITE | PROT_EXEC)
#define KNOWN_FLAGS (MAP_SHARED | MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS)

syscallret_t syscall_mmap(context_t *context, void *hint, size_t len, int prot, int flags, int fd, off_t offset) {
	syscallret_t ret = {
		.errno = 0,
		.ret = -1
	};

	__assert((~KNOWN_FLAGS & flags) == 0);
	__assert((~KNOWN_PROT & prot) == 0);

	// check alignment
	if (len == 0 || (uintptr_t)hint % PAGE_SIZE || len % PAGE_SIZE) {
		ret.errno = EINVAL;
		return ret;
	}

	mmuflags_t mmuflags = ARCH_MMU_FLAGS_USER;
	if (prot & PROT_READ)
		mmuflags |= ARCH_MMU_FLAGS_READ;
	if (prot & PROT_WRITE)
		mmuflags |= ARCH_MMU_FLAGS_WRITE;
	if ((prot & PROT_EXEC) == 0)
		mmuflags |= ARCH_MMU_FLAGS_NOEXEC;

	bool isfile = (flags & MAP_ANONYMOUS) == 0;
	int vmmflags = 0;

	// XXX this isn't the correct behaviour for MAP_FIXED
	if (flags & MAP_FIXED)
		vmmflags |= VMM_FLAGS_EXACT;

	if ((flags & MAP_ANONYMOUS) == 0)
		vmmflags |= VMM_FLAGS_FILE;

	if (flags & MAP_SHARED)
		vmmflags |= VMM_FLAGS_SHARED;

	vmmfiledesc_t vfd;
	file_t *file = NULL;
	if (isfile) {
		file = fd_get(fd);
		if (file == NULL) {
			ret.errno = EBADF;
			return ret;
		}
		vfd.node = file->vnode;
		vfd.offset = offset;
	}

	if (hint < USERSPACE_START)
		hint = USERSPACE_START;

	ret.ret = (uint64_t)vmm_map(hint, len, vmmflags, mmuflags, &vfd);
	if (ret.ret == 0)
		ret.errno = ENOMEM;

	if (isfile)
		fd_release(file);

	return ret;
}
