#include <kernel/syscalls.h>
#include <kernel/abi.h>
#include <kernel/vfs.h>
#include <arch/cpu.h>
#include <errno.h>
#include <kernel/file.h>
#include <kernel/alloc.h>

static int dostat(vnode_t *vnode, stat_t *stat) {
	vattr_t attr;
	int e = VOP_GETATTR(vnode, &attr, &_cpu()->thread->proc->cred);
	if (e)
		return e;

	stat->dev = 0; // TODO properly return this
	stat->rdev = TODEV(attr.rdevmajor, attr.rdevminor);
	stat->ino = attr.inode;
	stat->nlink = attr.nlinks;
	stat->mode = attr.mode | MAKETYPE(vfs_getposixtype(attr.type));
	stat->uid = attr.uid;
	stat->gid = attr.gid;
	stat->size = attr.size;
	stat->blksize = attr.fsblocksize;
	stat->blocks = attr.blocksused;
	stat->atim = attr.atime;
	stat->mtim = attr.mtime;
	stat->ctim = attr.ctime;

	return 0;
}

syscallret_t syscall_fstat(context_t *ctx, int fd, stat_t *ustat) {
	syscallret_t ret = {
		.ret = -1
	};
	file_t *file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		return ret;
	}

	stat_t buf;
	ret.errno = dostat(file->vnode, &buf);
	if (ret.errno)
		goto cleanup;

	// TODO safe memcpy
	*ustat = buf;

	cleanup:
	fd_release(file);
	return ret;
}

syscallret_t syscall_fstatat(context_t *ctx, int dirfd, char *upath, stat_t *ustat, int flags) {
	syscallret_t ret = {
		.ret = -1
	};

	char *path = alloc(strlen(upath) + 1);
	if (path == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}
	strcpy(path, upath);

	vnode_t *dirnode = NULL;
	file_t *file = NULL;
	ret.errno = dirfd_enter(path, dirfd, &file, &dirnode);
	if (ret.errno)
		goto cleanup;

	vnode_t *node = NULL;
	ret.errno = vfs_lookup(&node, dirnode, path, NULL, flags & AT_SYMLINK_NOFOLLOW ? VFS_LOOKUP_NOLINK : 0);
	if (ret.errno)
		goto cleanup;

	stat_t buf;
	ret.errno = dostat(node, &buf);
	if (ret.errno)
		goto cleanup;

	// TODO safe memcpy
	*ustat = buf;
	ret.ret = 0;

	cleanup:
	if (node)
		VOP_RELEASE(node);

	if (dirnode)
		dirfd_leave(dirnode, file);

	free(path);

	return ret;
}
