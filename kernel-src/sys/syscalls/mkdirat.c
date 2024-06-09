#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/vfs.h>
#include <kernel/alloc.h>
#include <kernel/abi.h>
#include <arch/cpu.h>

syscallret_t syscall_mkdirat(context_t *, int dirfd, char *upath, mode_t mode) {
	syscallret_t ret = {
		.ret = -1
	};

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

	vnode_t *dirnode = NULL;
	file_t *file = NULL;
	ret.errno = dirfd_enter(path, dirfd, &file, &dirnode);
	if (ret.errno)
		goto cleanup;

	vattr_t attr = {
		.mode = UMASK(mode),
		.gid = _cpu()->thread->proc->cred.gid,
		.uid = _cpu()->thread->proc->cred.uid
	};

	ret.errno = vfs_create(dirnode, path, &attr, V_TYPE_DIR, NULL);
	ret.ret = ret.errno ? -1 : 0;

	cleanup:

	if (dirnode)
		dirfd_leave(dirnode, file);

	free(path);

	return ret;
}
