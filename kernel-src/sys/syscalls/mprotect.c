#include <kernel/syscalls.h>
#include <kernel/abi.h>
#include <errno.h>
#include <kernel/mm.h>
#include <logging.h>

#define PROT_READ  0x01
#define PROT_WRITE 0x02
#define PROT_EXEC  0x04

#define KNOWN_PROT (PROT_READ | PROT_WRITE | PROT_EXEC)

syscallret_t syscall_mprotect(context_t *context, void *address, size_t len, int prot) {
	syscallret_t ret = {
		.errno = 0,
		.ret = -1
	};

	if (prot & ~KNOWN_PROT) {
		ret.errno = EINVAL;
		return ret;
	}

	// check alignment
	if (len == 0 || (uintptr_t)address % PAGE_SIZE || address > USERSPACE_END) {
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

	ret.errno = mm_change_mmu_flags(address, len, mmuflags, MM_RANGE_FLAGS_CREDCHECK);
	ret.ret = ret.errno ? -1 : 0;

	return ret;
}
