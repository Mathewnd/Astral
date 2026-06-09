#include <kernel/sysctl.h>
#include <errno.h>

// oldp, old_lenp and newp might be user pointers
int sysctl(const int *name, size_t name_len, void *oldp, size_t *old_lenp, void *newp, size_t new_len) {
	if (name_len < 2 || name_len > SYS_CTL_NAME_MAX)
		return EINVAL;

	switch (*name) {
		case SYS_CTL_KERN:
			return sysctl_kern(name + 1, name_len - 1, oldp, old_lenp, newp, new_len);
		default:
			return EINVAL;
	}
}
