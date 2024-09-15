#include <kernel/syscalls.h>
#include <kernel/scheduler.h>
#include <kernel/vfs.h>
#include <kernel/alloc.h>
#include <errno.h>
#include <kernel/auth.h>

syscallret_t syscall_chroot(context_t *, char *upath) {
	syscallret_t ret = {
		.ret = -1
	};

	ret.errno = auth_system_check(&current_thread()->proc->cred, AUTH_ACTIONS_SYSTEM_CHROOT);
	if (ret.errno)
		return ret;

	size_t pathlen;
	ret.errno = usercopy_strlen(upath, &pathlen);
	if (ret.errno)
		return ret;

	char *path = alloc(pathlen + 1);
	if (path == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	ret.errno = usercopy_fromuser(path, upath, pathlen);
	if (ret.errno) {
		free(path);
		return ret;
	}

	vnode_t *ref = path[0] == '/' ? sched_getroot() : sched_getcwd();
	vnode_t *new = NULL;

	ret.errno = vfs_lookup(&new, ref, path, NULL, 0);
	if (ret.errno)
		goto cleanup;

	VOP_UNLOCK(new);

	if (new->type != V_TYPE_DIR) {
		ret.errno = ENOTDIR;
		goto cleanup;
	}

	sched_setroot(new);
	ret.ret = 0;

	cleanup:
	if (new)
		VOP_RELEASE(new);

	free(path);
	VOP_RELEASE(ref);

	return ret;
}
