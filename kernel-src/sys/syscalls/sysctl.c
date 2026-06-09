#include <kernel/syscalls.h>
#include <kernel/sysctl.h>
#include <arch/mmu.h>

syscallret_t syscall_sysctl(context_t *, int *namep, size_t name_len, void *oldp, size_t *old_lenp, void *newp, size_t new_len) {
	syscallret_t ret = {
		.ret = -1
	};

	if (name_len > SYS_CTL_NAME_MAX) {
		ret.errno = EINVAL;
		return ret;
	}

	if (!IS_USER_ADDRESS(namep) || !IS_USER_ADDRESS(oldp) || !IS_USER_ADDRESS(old_lenp) || !IS_USER_ADDRESS(newp)) {
		ret.errno = EFAULT;
		return ret;
	}

	int name[name_len];
	ret.errno = usercopy_fromuser(name, namep, sizeof(name));
	if (ret.errno)
		return ret;

	ret.errno = sysctl(name, name_len, oldp, old_lenp, newp, new_len);
	ret.ret = ret.errno ? -1 : 0;
	return ret;
}
