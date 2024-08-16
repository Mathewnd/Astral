#include <kernel/abi.h>
#include <kernel/syscalls.h>
#include <kernel/cred.h>

syscallret_t syscall_setresuid(context_t *, uid_t uid, uid_t euid, uid_t suid) {
	syscallret_t ret;

	ret.errno = cred_setuids(&_cpu()->thread->proc->cred, uid, euid, suid);
	ret.ret = ret.errno ? -1 : 0;

	return ret;
}
