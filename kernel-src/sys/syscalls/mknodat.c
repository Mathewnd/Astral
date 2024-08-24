#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/vfs.h>
#include <kernel/alloc.h>
#include <kernel/abi.h>
#include <arch/cpu.h>

syscallret_t syscall_mknodat(context_t *, int dirfd, char *upath, mode_t mode, dev_t dev) {
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

	int type = vfs_getsystemtype(GETTYPE(mode));
	mode = GETMODE(mode);

	vattr_t attr = {
		.mode = UMASK(mode),
		.gid = _cpu()->thread->proc->cred.egid,
		.uid = _cpu()->thread->proc->cred.euid,
		.rdevmajor = (type == V_TYPE_CHDEV || type == V_TYPE_BLKDEV) ? MAJORDEV(dev) : 0,
		.rdevminor = (type == V_TYPE_CHDEV || type == V_TYPE_BLKDEV) ? MINORDEV(dev) : 0,
	};

	ret.errno = vfs_create(dirnode, path, &attr, type, NULL);
	ret.ret = ret.errno ? -1 : 0;

	cleanup:

	if (dirnode)
		dirfd_leave(dirnode, file);

	free(path);

	return ret;
}
