#include <kernel/abi.h>
#include <kernel/syscalls.h>
#include <kernel/cred.h>

syscallret_t syscall_getresuid(context_t *, uid_t *uidp, uid_t *euidp, uid_t *suidp) {
	syscallret_t ret = {
		.ret = -1
	};

	int uid, euid, suid;
	cred_getuids(&_cpu()->thread->proc->cred, &uid, &euid, &suid);

	ret.errno = usercopy_touser(uidp, &uid, sizeof(uid));
	if (ret.errno)
		goto leave;

	ret.errno = usercopy_touser(euidp, &euid, sizeof(euid));
	if (ret.errno)
		goto leave;

	ret.errno = usercopy_touser(suidp, &suid, sizeof(suid));
	if (ret.errno)
		goto leave;

	ret.ret = 0;

	leave:
	return ret;
}
