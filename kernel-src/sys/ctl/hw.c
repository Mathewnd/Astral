#include <kernel/sysctl.h>
#include <kernel/usercopy.h>
#include <arch/smp.h>

static int ctl_cpus_online(const int *, size_t name_len, void *oldp, size_t *old_lenp, void *newp, size_t) {
	if (name_len != 0)
		return EINVAL;

	if (newp)
		return EPERM;

	if (!old_lenp)
		return EINVAL;

	size_t old_len = 0;
	if (oldp) {
		int error = USERCOPY_POSSIBLY_FROM_USER(&old_len, old_lenp, sizeof(old_len));
		if (error)
			return error;
	}

	size_t required_len = sizeof(size_t);
	int error = USERCOPY_POSSIBLY_TO_USER(old_lenp, &required_len, sizeof(required_len));
	if (error)
		return error;

	if (!oldp)
		return 0;

	if (old_len < required_len)
		return ENOMEM;

	size_t cpus_online = arch_smp_get_cpu_count();
	return USERCOPY_POSSIBLY_TO_USER(oldp, &cpus_online, sizeof(cpus_online));
}

int sysctl_hw(const int *name, size_t name_len, void *oldp, size_t *old_lenp, void *newp, size_t new_len) {
	switch (*name) {
		case SYS_CTL_HW_CPUS_ONLINE:
			return ctl_cpus_online(name + 1, name_len - 1, oldp, old_lenp, newp, new_len);
	}

	return EINVAL;
}
