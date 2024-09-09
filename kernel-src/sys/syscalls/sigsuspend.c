#include <kernel/syscalls.h>
#include <kernel/signal.h>
#include <logging.h>

syscallret_t syscall_sigsuspend(context_t *, sigset_t *umask) {
	syscallret_t ret = {
		.ret = -1
	};

	sigset_t mask;

	ret.errno = usercopy_fromuser(&mask, umask, sizeof(sigset_t));
	if (ret.errno)
		return ret;

	signal_suspend(&mask);

	ret.errno = EINTR;
	return ret;
}
