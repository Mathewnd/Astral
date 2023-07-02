#include <kernel/syscalls.h>
#include <kernel/scheduler.h>
#include <kernel/vfs.h>
#include <kernel/alloc.h>
#include <errno.h>

syscallret_t syscall_chdir(context_t *, const char *upath) {
	syscallret_t ret = {
		.ret = -1
	};

	if (upath > (char *)USERSPACE_END) {
		ret.errno = EFAULT;
		return ret;
	}

	// TODO safe string ops
	char *path = alloc(strlen(upath) + 1);
	strcpy(path, upath);

	vnode_t *ref = path[0] == '/' ? sched_getroot() : sched_getcwd();
	vnode_t *new = NULL;

	ret.errno = vfs_lookup(&new, ref, path, NULL, 0);
	if (ret.errno)
		goto cleanup;

	if (new->type != V_TYPE_DIR) {
		ret.errno = ENOTDIR;
		goto cleanup;
	}

	sched_setcwd(new);
	ret.ret = 0;

	cleanup:
	if (new)
		VOP_RELEASE(new);

	free(path);
	VOP_RELEASE(ref);

	return ret;
}
