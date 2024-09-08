#include <kernel/syscalls.h>
#include <kernel/signal.h>
#include <time.h>
#include <logging.h>

syscallret_t syscall_sigtimedwait(context_t *, sigset_t *uset, siginfo_t *uinfo, timespec_t *utimeout) {
	syscallret_t ret = {
		.ret = -1
	};

	sigset_t set;
	timespec_t timeout;
	siginfo_t info;
	int signum;

	ret.errno = usercopy_fromuser(&set, uset, sizeof(sigset_t));
	if (ret.errno)
		return ret;

	ret.errno = usercopy_fromuser(&timeout, utimeout, sizeof(timespec_t));
	if (ret.errno)
		return ret;

	ret.errno = signal_wait(&set, &timeout, &info, &signum);
	if (ret.errno)
		return ret;

	if (uinfo)
		ret.errno = usercopy_touser(uinfo, &info, sizeof(siginfo_t));

	ret.ret = ret.errno ? -1 : 0;
	return ret;
}
