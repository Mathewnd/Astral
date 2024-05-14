#include <kernel/syscalls.h>
#include <arch/cpu.h>
#include <logging.h>

syscallret_t syscall_sigaction(context_t *, int sig, sigaction_t *new, sigaction_t *old) {
	syscallret_t ret = {
		.ret = -1
	};

	if (sig > NSIG || sig == SIGKILL || sig == SIGSTOP) {
		ret.errno = EINVAL;
		return ret;
	}

	sigaction_t newtmp, oldtmp;
	if (new) {
		newtmp = *new;
		__assert(newtmp.flags & SA_RESTORER);
	}

	signal_action(_cpu()->thread->proc, sig, new ? &newtmp : NULL, old ? &oldtmp : NULL);

	if (old)
		*old = oldtmp;

	ret.errno = 0;
	ret.ret = 0;
	return ret;
}
