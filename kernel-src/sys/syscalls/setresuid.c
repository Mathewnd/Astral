#include <kernel/abi.h>
#include <kernel/syscalls.h>
#include <kernel/cred.h>

syscallret_t syscall_setresuid(context_t *, uid_t uid, uid_t euid, uid_t suid) {
	syscallret_t ret;

	ret.errno = cred_setuids(&_cpu()->thread->proc->cred, uid, euid, suid);
	ret.ret = ret.errno ? -1 : 0;

	return ret;
}

syscallret_t syscall_setuid(context_t *, uid_t uid) {
	syscallret_t ret;

	ret.errno = cred_setuid(&_cpu()->thread->proc->cred, uid);
	ret.ret = ret.errno ? -1 : 0;

	return ret;
}

syscallret_t syscall_seteuid(context_t *, uid_t euid) {
	syscallret_t ret;

	ret.errno = cred_seteuid(&_cpu()->thread->proc->cred, euid);
	ret.ret = ret.errno ? -1 : 0;

	return ret;
}

syscallret_t syscall_setgid(context_t *, gid_t gid) {
	syscallret_t ret;

	ret.errno = cred_setgid(&_cpu()->thread->proc->cred, gid);
	ret.ret = ret.errno ? -1 : 0;

	return ret;
}

syscallret_t syscall_setegid(context_t *, uid_t egid) {
	syscallret_t ret;

	ret.errno = cred_setegid(&_cpu()->thread->proc->cred, egid);
	ret.ret = ret.errno ? -1 : 0;

	return ret;
}
