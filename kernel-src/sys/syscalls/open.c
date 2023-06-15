#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/abi.h>
#include <kernel/alloc.h>
#include <errno.h>
#include <logging.h>

syscallret_t syscall_openat(context_t *context, int dirfd, const char *path, int flags, mode_t mode) {
	syscallret_t ret = {
		.ret = -1,
		.errno = 0
	};

	// transform O_RDONLY, O_WRONLY, O_RDWR into FILE_READ and FILE_WRITE
	++flags;

	vnode_t *dirnode = NULL;
	file_t *dirfile;

	size_t pathsize = strlen(path); // TODO u_strlen
	char *pathbuf = alloc(pathsize + 1);
	strcpy(pathbuf, path); // TODO u_strcpy

	if (*pathbuf == '/') {
		dirnode = sched_getroot();
		dirfile = NULL;
	} else if (dirfd == AT_FDCWD) {
		dirnode = sched_getcwd();
		dirfile = NULL;
	} else {
		dirfile = fd_get(dirfd);
		if (dirfile == NULL) {
			ret.errno = EBADF;
			goto cleanup;
		}

		dirnode = dirfile->vnode;
		if (dirnode->type != V_TYPE_DIR) {
			ret.errno = ENOTDIR;
			goto cleanup;
		}
	}

	// TODO pass CLOEXEC to fd
	file_t *newfile = NULL;
	int newfd;
	ret.errno = fd_new(0, &newfile, &newfd);
	if (ret.errno)
		goto cleanup;

	vnode_t *vnode = NULL;
	ret.errno = vfs_open(dirnode, pathbuf, fileflagstovnodeflags(flags), &vnode);
	if (ret.errno)
		goto cleanup;

	vattr_t attr;
	ret.errno = VOP_GETATTR(vnode, &attr, &_cpu()->thread->proc->cred);
	if (ret.errno)
		goto cleanup;

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

	if (newfile) {
		if (ret.errno) {
			__assert(fd_close(newfd) == 0);
		} else {
			fd_release(newfile);
		}
	}

	if (pathbuf)
		free(pathbuf);

	if (dirfile) {
		fd_release(dirfile);
	} else if (dirnode){
		VOP_RELEASE(dirnode);
	}

	return ret;
}
