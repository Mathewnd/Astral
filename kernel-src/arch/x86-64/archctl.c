#include <arch/cpu.h>
#include <kernel/syscalls.h>
#include <arch/msr.h>
#include <errno.h>

#define ARCH_CTL_SET_GSBASE 0
#define ARCH_CTL_SET_FSBASE 1
#define ARCH_CTL_GET_GSBASE 2
#define ARCH_CTL_GET_FSBASE 3
#define ARCH_CTL_SET_SYSTRACE 4

#define ARCH_CTL_SYSTRACE_OFF 0
#define ARCH_CTL_SYSTRACE_SELF 1
#define ARCH_CTL_SYSTRACE_ALL 2

static inline int copy_u64_to_user(void *ptr, uint64_t v) {
	return usercopy_touser(ptr, &v, sizeof(v));
}

syscallret_t syscall_archctl(context_t *context, int func, void *arg) {
	syscallret_t ret = {
		.errno = 0,
		.ret = 0
	};

	if (!IS_USER_ADDRESS(arg)) {
		ret.errno = EFAULT;
		return ret;
	}

	switch (func) {
		case ARCH_CTL_SET_GSBASE:
			// kernelgsbase because it will be switched out to user
			wrmsr(MSR_KERNELGSBASE, (uint64_t)arg);
			break;
		case ARCH_CTL_SET_FSBASE:
			wrmsr(MSR_FSBASE, (uint64_t)arg);
			break;
		case ARCH_CTL_GET_GSBASE:
			ret.errno = copy_u64_to_user(arg, rdmsr(MSR_KERNELGSBASE));
			break;
		case ARCH_CTL_GET_FSBASE:
			ret.errno = copy_u64_to_user(arg, rdmsr(MSR_FSBASE));
			break;
		case ARCH_CTL_SET_SYSTRACE:
#ifdef SYSCALL_LOGGING
			switch ((uint64_t)arg) {
				case ARCH_CTL_SYSTRACE_OFF:
					current_thread()->proc->flags &= ~(PROC_FLAG_SYSTRACE | PROC_FLAG_SYSTRACE_SELF);
					break;
				case ARCH_CTL_SYSTRACE_SELF:
					current_thread()->proc->flags |= PROC_FLAG_SYSTRACE | PROC_FLAG_SYSTRACE_SELF;
					break;
				case ARCH_CTL_SYSTRACE_ALL:
					current_thread()->proc->flags |= PROC_FLAG_SYSTRACE;
					current_thread()->proc->flags &= ~PROC_FLAG_SYSTRACE_SELF;
					break;
				default:
					ret.errno = EINVAL;
					break;
			}
#else
			// signal that syscall logging is not supported
			ret.errno = ENOTSUP;
#endif
			break;
		default:
			ret.errno = EINVAL;
			break;
	}

	return ret;
}
