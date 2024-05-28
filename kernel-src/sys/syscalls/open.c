#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/abi.h>
#include <kernel/alloc.h>
#include <errno.h>
#include <logging.h>

syscallret_t syscall_openat(context_t *context, int dirfd, const char *path, int flags, mode_t mode) {
	syscallret_t ret = {
		.ret = -1,
		.errno = -1
	};

	// transform O_RDONLY, O_WRONLY, O_RDWR into FILE_READ and FILE_WRITE
	++flags;

	vnode_t *dirnode = NULL;
	file_t *dirfile = NULL;

	size_t pathsize = strlen(path); // TODO u_strlen
	char *pathbuf = alloc(pathsize + 1);
	if (pathbuf == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	strcpy(pathbuf, path); // TODO u_strcpy

	ret.errno = dirfd_enter(pathbuf, dirfd, &dirfile, &dirnode);

	retry_open:
	file_t *newfile = NULL;
	int newfd;
	ret.errno = fd_new(flags & O_CLOEXEC, &newfile, &newfd);
	if (ret.errno)
		goto cleanup;

	vnode_t *vnode = NULL;
	ret.errno = vfs_open(dirnode, pathbuf, fileflagstovnodeflags(flags), &vnode);
	if (ret.errno == 0 && (flags & O_CREAT) && (flags & O_EXCL)) {
		ret.errno = EEXIST;
		return ret;
	}

	if (ret.errno == ENOENT && (flags & O_CREAT)) {
		vattr_t attr = {
			.mode = UMASK(mode),
			.gid = _cpu()->thread->proc->cred.gid,
			.uid = _cpu()->thread->proc->cred.uid
		};

		ret.errno = vfs_create(dirnode, pathbuf, &attr, V_TYPE_REGULAR, &vnode);
		if (ret.errno == EEXIST && (flags & O_EXCL) == 0)
			goto retry_open;
	}

	if (ret.errno)
		goto cleanup;

	if ((flags & O_DIRECTORY) && vnode->type != V_TYPE_DIR) {
		ret.errno = ENOTDIR;
		goto cleanup;
	}

	vattr_t attr;
	ret.errno = VOP_GETATTR(vnode, &attr, &_cpu()->thread->proc->cred);
	if (ret.errno)
		goto cleanup;

	if (vnode->type == V_TYPE_REGULAR && (flags & O_TRUNC)) {
		MUTEX_ACQUIRE(&vnode->sizelock, false);
		ret.errno = VOP_RESIZE(vnode, 0, &_cpu()->thread->proc->cred);
		MUTEX_RELEASE(&vnode->sizelock);
		if (ret.errno)
			goto cleanup;
	}

	// node refcount is already increased by open
	newfile->vnode = vnode;
	newfile->flags = flags;
	newfile->offset = 0;
	newfile->mode = attr.mode;

	ret.errno = 0;
	ret.ret = newfd;

	cleanup:
	if (vnode && ret.errno) {
		vfs_close(vnode, fileflagstovnodeflags(flags));
		VOP_RELEASE(vnode);
	}

	if (newfile && ret.errno)
		__assert(fd_close(newfd) == 0);


	if (pathbuf)
		free(pathbuf);

	if (dirnode)
		dirfd_leave(dirnode, dirfile);

	return ret;
}
