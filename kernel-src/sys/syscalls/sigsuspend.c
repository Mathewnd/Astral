#include <kernel/syscalls.h>
#include <kernel/signal.h>
#include <logging.h>

syscallret_t syscall_sigsuspend(context_t *, sigset_t *umask) {
	syscallret_t ret = {
		.ret = -1
	};

	sigset_t mask;
	sigset_t old;

	ret.errno = usercopy_fromuser(&mask, umask, sizeof(sigset_t));
	if (ret.errno)
		return ret;

	signal_changemask(_cpu()->thread, SIG_SETMASK, &mask, &old);

	sched_preparesleep(true);
	__assert(sched_yield() == SCHED_WAKEUP_REASON_INTERRUPTED);

	signal_changemask(_cpu()->thread, SIG_SETMASK, &old, NULL);

	ret.errno = EINTR;
	return ret;
}
