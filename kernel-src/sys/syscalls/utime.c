#include <kernel/syscalls.h>
#include <kernel/alloc.h>
#include <kernel/timekeeper.h>

syscallret_t syscall_utimensat(context_t *, int fd, char *upath, timespec_t uts[2], int flags) {
	syscallret_t ret = {
		.ret = -1
	};

	timespec_t ts[2];
	if (uts == NULL)
		ts[0] = ts[1] = timekeeper_time();
	else
		memcpy(ts, uts, sizeof(timespec_t) * 2);

	vnode_t *node = NULL;
	vnode_t *dirnode = NULL;
	file_t *file = NULL;
	char *path = NULL;

	if (upath == NULL) {
		// operation is done on the file pointed to by fd
		file_t *file = fd_get(fd);
		if (file == NULL) {
			ret.errno = EBADF;
			goto cleanup;
		}

		node = file->vnode;
		VOP_HOLD(node);
		fd_release(file);
	} else {
		// operation is done on the file pointed to by the path relative to fd
		path = alloc(strlen(upath) + 1);
		if (path == NULL) {
			ret.errno = ENOMEM;
			goto cleanup;
		}

		strcpy(path, upath);
		ret.errno = dirfd_enter(path, fd, &file, &dirnode);
		if (ret.errno)
			goto cleanup;

		ret.errno = vfs_lookup(&node, dirnode, path, NULL, (flags & AT_SYMLINK_NOFOLLOW) ? VFS_LOOKUP_NOLINK : 0);
		if (ret.errno)
			goto cleanup;
	}

	vattr_t attr;
	ret.errno = VOP_GETATTR(node, &attr, &_cpu()->thread->proc->cred);
	if (ret.errno)
		goto cleanup;

	attr.atime = ts[0];
	attr.mtime = ts[1];

	ret.errno = VOP_SETATTR(node, &attr, V_ATTR_ATIME | V_ATTR_MTIME, &_cpu()->thread->proc->cred);
	ret.ret = ret.errno ? -1 : 0;

	cleanup:
	if (node)
		VOP_RELEASE(node);

	if (dirnode)
		dirfd_leave(dirnode, file);

	if (path)
		free(path);

	return ret;
}
