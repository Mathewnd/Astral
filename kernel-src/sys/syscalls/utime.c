#include <kernel/syscalls.h>
#include <kernel/alloc.h>
#include <kernel/timekeeper.h>
#include <kernel/auth.h>

syscallret_t syscall_utimensat(context_t *, int fd, char *upath, timespec_t uts[2], int flags) {
	syscallret_t ret = {
		.ret = -1,
		.errno = 0
	};

	timespec_t ts[2];
	if (uts == NULL)
		ts[0] = ts[1] = timekeeper_time();
	else
		ret.errno = usercopy_fromuser(ts, uts, sizeof(timespec_t) * 2);

	if (ret.errno)
		return ret;

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
		VOP_LOCK(node);
		VOP_HOLD(node);
		fd_release(file);
	} else {
		// operation is done on the file pointed to by the path relative to fd
		size_t pathlen;
		ret.errno = usercopy_strlen(upath, &pathlen);
		if (ret.errno)
			goto cleanup;

		path = alloc(pathlen + 1);
		if (path == NULL) {
			ret.errno = ENOMEM;
			goto cleanup;
		}

		ret.errno = usercopy_fromuser(path, upath, pathlen);
		if (ret.errno)
			goto cleanup;

		ret.errno = dirfd_enter(path, fd, &file, &dirnode);
		if (ret.errno)
			goto cleanup;

		ret.errno = vfs_lookup(&node, dirnode, path, NULL, (flags & AT_SYMLINK_NOFOLLOW) ? VFS_LOOKUP_NOLINK : 0);
		if (ret.errno)
			goto cleanup;
	}

	vattr_t attr;
	ret.errno = VOP_GETATTR(node, &attr, &_cpu()->thread->proc->cred);
	if (ret.errno) {
		VOP_UNLOCK(node);
		goto cleanup;
	}

	attr.atime = ts[0];
	attr.mtime = ts[1];

	ret.errno = auth_filesystem_check(&_cpu()->thread->proc->cred, AUTH_ACTIONS_FILESYSTEM_SETATTR, node, NULL);
	if (ret.errno) {
		if (uts == NULL)
			ret.errno = VOP_ACCESS(node, V_ACCESS_WRITE, &_cpu()->thread->proc->cred);

		if (ret.errno) {
			VOP_UNLOCK(node);
			goto cleanup;
		}
	}

	ret.errno = VOP_SETATTR(node, &attr, V_ATTR_ATIME | V_ATTR_MTIME, &_cpu()->thread->proc->cred);
	ret.ret = ret.errno ? -1 : 0;
	VOP_UNLOCK(node);

	cleanup:
	if (node)
		VOP_RELEASE(node);

	if (dirnode)
		dirfd_leave(dirnode, file);

	if (path)
		free(path);

	return ret;
}
