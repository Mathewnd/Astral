#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/vfs.h>
#include <kernel/alloc.h>
#include <kernel/abi.h>
#include <arch/cpu.h>

syscallret_t syscall_mkdirat(context_t *, int dirfd, const char *upath, mode_t mode) {
	syscallret_t ret = {
		.ret = -1
	};

	char *path = alloc(strlen(upath) + 1);
	if (path == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	// TODO safe strcopy
	strcpy(path, upath);

	vnode_t *dirnode = NULL;
	file_t *file = NULL;
	if (*path == '/') {
		dirnode = sched_getroot();
	} else if (dirfd == AT_FDCWD) {
		dirnode = sched_getcwd();
	} else {
		file = fd_get(dirfd);
		if (file == NULL) {
			ret.errno = EBADF;
			goto cleanup;
		}

		dirnode = file->vnode;

		if (dirnode->type != V_TYPE_DIR) {
			ret.errno = ENOTDIR;
			goto cleanup;
		}
	}

	vattr_t attr = {
		.mode = mode,
		.gid = _cpu()->thread->proc->cred.gid,
		.uid = _cpu()->thread->proc->cred.uid
	};

	ret.errno = vfs_create(dirnode, path, &attr, V_TYPE_DIR, NULL);
	ret.ret = ret.errno ? -1 : 0;

	cleanup:

	if (file)
		fd_release(file);
	else
		VOP_RELEASE(dirnode);

	free(path);

	return ret;
}
