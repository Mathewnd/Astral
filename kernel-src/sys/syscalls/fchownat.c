#include <kernel/syscalls.h>
#include <kernel/alloc.h>
#include <kernel/vfs.h>

syscallret_t syscall_fchownat(context_t *, int fd, const char *upath, uid_t owner, gid_t group, int flags) {
	syscallret_t ret = {
		.ret = -1
	};

	vnode_t *node = NULL;
	vnode_t *dirnode = NULL;
	file_t *file = NULL;
	char *path = NULL;
	size_t pathlen = strlen(upath);

	if (pathlen == 0 && (flags & AT_EMPTY_PATH)) {
		// chown is done on the file pointed to by fd
		file_t *file = fd_get(fd);
		if (file == NULL) {
			ret.errno = EBADF;
			goto cleanup;
		}

		node = file->vnode;
		VOP_HOLD(node);
		fd_release(file);
	} else {
		// chown is done on the file pointed to by the path relative to fd
		path = alloc(pathlen + 1);
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

	attr.uid = owner;
	attr.gid = group;

	ret.errno = VOP_SETATTR(node, &attr, V_ATTR_UID | V_ATTR_GID, &_cpu()->thread->proc->cred);
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
