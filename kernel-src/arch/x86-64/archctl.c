#include <kernel/syscalls.h>
#include <arch/msr.h>
#include <errno.h>

#define ARCH_CTL_GSBASE 0
#define ARCH_CTL_FSBASE 1

syscallret_t syscall_archctl(context_t *context, int func, void *arg) {
	syscallret_t ret = {
		.errno = 0,
		.ret = 0
	};

	switch (func) {
		case ARCH_CTL_GSBASE:
			// kernelgsbase because it will be switched out to user
			wrmsr(MSR_KERNELGSBASE, (uint64_t)arg);
			break;
		case ARCH_CTL_FSBASE:
			wrmsr(MSR_FSBASE, (uint64_t)arg);
			break;
		default:
			ret.errno = EINVAL;
			break;
	}

	return ret;
}
