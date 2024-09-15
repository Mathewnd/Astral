#include <kernel/abi.h>
#include <kernel/syscalls.h>
#include <kernel/cred.h>

syscallret_t syscall_setresgid(context_t *, gid_t gid, gid_t egid, gid_t sgid) {
	syscallret_t ret;

	ret.errno = cred_setgids(&current_thread()->proc->cred, gid, egid, sgid);
	ret.ret = ret.errno ? -1 : 0;

	return ret;
}
