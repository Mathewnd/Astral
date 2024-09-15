#include <kernel/abi.h>
#include <kernel/syscalls.h>
#include <kernel/cred.h>

syscallret_t syscall_getresgid(context_t *, gid_t *gidp, gid_t *egidp, gid_t *sgidp) {
	syscallret_t ret = {
		.ret = -1
	};

	int gid, egid, sgid;
	cred_getgids(&current_thread()->proc->cred, &gid, &egid, &sgid);

	ret.errno = usercopy_touser(gidp, &gid, sizeof(gid));
	if (ret.errno)
		goto leave;

	ret.errno = usercopy_touser(egidp, &egid, sizeof(egid));
	if (ret.errno)
		goto leave;

	ret.errno = usercopy_touser(sgidp, &sgid, sizeof(sgid));
	if (ret.errno)
		goto leave;

	ret.ret = 0;

	leave:
	return ret;
}
