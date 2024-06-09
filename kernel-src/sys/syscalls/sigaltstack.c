#include <kernel/syscalls.h>
#include <arch/cpu.h>

syscallret_t syscall_sigaltstack(context_t *, stack_t *new, stack_t *old) {
	syscallret_t ret = {
		.ret = -1
	};

	stack_t newtmp, oldtmp;
	if (new && usercopy_fromuser(&newtmp, new, sizeof(stack_t))) {
		ret.errno = EFAULT;
		return ret;
	}

	signal_altstack(_cpu()->thread, new ? &newtmp : NULL, old ? &oldtmp : NULL);

	if (old && usercopy_touser(old, &oldtmp, sizeof(stack_t))) {
		ret.errno = EFAULT;
		return ret;
	}

	ret.errno = 0;
	ret.ret = 0;
	return ret;
}
