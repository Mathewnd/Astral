#include <kernel/syscalls.h>
#include <kernel/scheduler.h>
#include <kernel/vfs.h>
#include <kernel/alloc.h>
#include <errno.h>

syscallret_t syscall_chdir(context_t *, char *upath) {
	syscallret_t ret = {
		.ret = -1
	};

	size_t pathlen = 0;
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

syscallret_t syscall_fchdir(context_t *, int fd) {
	syscallret_t ret = {
		.ret = -1
	};

	file_t *file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		return ret;
	}

	if (file->vnode->type != V_TYPE_DIR) {
		ret.errno = ENOTDIR;
		goto cleanup;
	}

	sched_setcwd(file->vnode);
	ret.errno = 0;

	cleanup:
	fd_release(file);
	return ret;
}
