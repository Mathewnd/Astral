#include <kernel/syscalls.h>
#include <kernel/signal.h>
#include <logging.h>

syscallret_t syscall_sigpending(context_t *, sigset_t *uset) {
	syscallret_t ret;

	sigset_t set;
	signal_pending(current_thread(), &set);

	ret.errno = usercopy_touser(uset, &set, sizeof(sigset_t));
	ret.ret = ret.errno ? -1 : 0;
	return ret;
}
