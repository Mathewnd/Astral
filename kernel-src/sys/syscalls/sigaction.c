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
		ret.errno = usercopy_fromuser(&newtmp, new, sizeof(sigaction_t));
		if (ret.errno)
			return ret;

		if ((newtmp.flags & SA_RESTORER) == 0) {
			ret.errno = EINVAL;
			return ret;
		}
	}

	signal_action(current_thread()->proc, sig, new ? &newtmp : NULL, old ? &oldtmp : NULL);

	if (old && usercopy_touser(old, &oldtmp, sizeof(sigaction_t))) {
		ret.errno = EFAULT;
		return ret;
	}

	ret.errno = 0;
	ret.ret = 0;
	return ret;
}
