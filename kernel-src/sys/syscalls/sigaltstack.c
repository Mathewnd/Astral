#include <kernel/syscalls.h>
#include <arch/cpu.h>

syscallret_t syscall_sigaltstack(context_t *, stack_t *new, stack_t *old) {
	syscallret_t ret = {
		.ret = -1
	};

	stack_t newtmp, oldtmp;
	if (new)
		newtmp = *new;

	signal_altstack(_cpu()->thread, new ? &newtmp : NULL, old ? &oldtmp : NULL);

	if (old)
		*old = oldtmp;

	ret.errno = 0;
	ret.ret = 0;
	return ret;
}
