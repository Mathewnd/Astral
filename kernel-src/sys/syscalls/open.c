#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/abi.h>
#include <kernel/alloc.h>
#include <errno.h>
#include <logging.h>
#include <kernel/auth.h>

syscallret_t syscall_openat(context_t *context, int dirfd, char *path, int flags, mode_t mode) {
	syscallret_t ret = {
		.ret = -1,
		.errno = -1
	};

	// transform O_RDONLY, O_WRONLY, O_RDWR into FILE_READ and FILE_WRITE
	++flags;

	vnode_t *dirnode = NULL;
	file_t *dirfile = NULL;

	size_t pathsize;
	ret.errno = usercopy_strlen(path, &pathsize);
	char *pathbuf = alloc(pathsize + 1);
	if (pathbuf == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	ret.errno = usercopy_fromuser(pathbuf, path, pathsize);
	if (ret.errno) {
		free(pathbuf);
		return ret;
	}

	ret.errno = dirfd_enter(pathbuf, dirfd, &dirfile, &dirnode);
	if (ret.errno) {
		free(pathbuf);
		return ret;
	}

	retry_open:
	file_t *newfile = NULL;
	int newfd;
	ret.errno = fd_new(flags & O_CLOEXEC, &newfile, &newfd);
	if (ret.errno)
		goto cleanup_nounlock;

	vnode_t *vnode = NULL;
	ret.errno = vfs_open(dirnode, pathbuf, fileflagstovnodeflags(flags), &vnode);
	if (ret.errno == 0 && (flags & O_CREAT) && (flags & O_EXCL)) {
		// exclusive and file already exists
		ret.errno = EEXIST;
		goto cleanup_nounlock;
	} else if (ret.errno == ENOENT && (flags & O_CREAT)) {
		// create file
		vattr_t attr = {
			.mode = UMASK(mode),
			.gid = _cpu()->thread->proc->cred.egid,
			.uid = _cpu()->thread->proc->cred.euid
		};

		ret.errno = vfs_create(dirnode, pathbuf, &attr, V_TYPE_REGULAR, &vnode);
		if (ret.errno == EEXIST && (flags & O_EXCL) == 0)
			goto retry_open;
	} else if (ret.errno) {
		// any other error
		goto cleanup_nounlock;
	} else {
		// success, lock file
		VOP_LOCK(vnode);
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

	if (vnode->type == V_TYPE_REGULAR && (flags & O_TRUNC) && (flags & FILE_WRITE)) {
		ret.errno = VOP_RESIZE(vnode, 0, &_cpu()->thread->proc->cred);
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

	if (vnode)
		VOP_UNLOCK(vnode);

	cleanup_nounlock:
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
