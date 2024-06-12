#include <kernel/file.h>
#include <kernel/slab.h>
#include <kernel/interrupt.h>
#include <logging.h>
#include <errno.h>
#include <kernel/alloc.h>

// TODO have a limit for the fd table

static scache_t *filecache;
#define FILE_HOLD(f) __atomic_add_fetch(&(f)->refcount, 1, __ATOMIC_SEQ_CST)

#define FILE_RELEASE(f) \
		if (__atomic_sub_fetch(&(f)->refcount, 1, __ATOMIC_SEQ_CST) == 0) {\
			cleanfile(f); \
		}

static void ctor(scache_t *cache, void *obj) {
	file_t *file = obj;
	file->vnode = NULL;
	MUTEX_INIT(&file->mutex);
	file->refcount = 1;
	file->offset = 0;
}

static file_t* newfile() {
	if (filecache == NULL) {
		filecache = slab_newcache(sizeof(file_t), 0, ctor, ctor);
		__assert(filecache);
	}

	return slab_allocate(filecache);
}

static void cleanfile(file_t *file) {
	if (file->vnode) {
		vfs_close(file->vnode, fileflagstovnodeflags(file->flags));
		VOP_RELEASE(file->vnode);
	}
	slab_free(filecache, file);
}

file_t *fd_allocate() {
	return newfile();
}

static int getfree(int start) {
	proc_t *proc = _cpu()->thread->proc;
	int fd = -1;
	int sfd;
	for (sfd = start; sfd < proc->fdcount && proc->fd[sfd].file; ++sfd);

	if (sfd != proc->fdcount)
		fd = sfd;

	return fd;
}

static int growtable(int newcount) {
	if (newcount > FDTABLE_LIMIT)
		return EMFILE;

	proc_t *proc = _cpu()->thread->proc;
	__assert(newcount > proc->fdcount);
	void *newtable = realloc(proc->fd, sizeof(fd_t) * newcount);
	if (newtable == NULL)
		return ENOMEM;

	proc->fd = newtable;
	proc->fdcount = newcount;
	return 0;
}

file_t *fd_get(int fd) {
	proc_t *proc = _cpu()->thread->proc;
	MUTEX_ACQUIRE(&proc->fdmutex, false);

	file_t *file = fd < proc->fdcount ? proc->fd[fd].file : NULL;
	if (file)
		FILE_HOLD(file);

	MUTEX_RELEASE(&proc->fdmutex);

	return file;
}

int fd_setflags(int fd, int flags) {
	int error = 0;
	proc_t *proc = _cpu()->thread->proc;
	MUTEX_ACQUIRE(&proc->fdmutex, false);

	if (proc->fd[fd].file)
		proc->fd[fd].flags = flags;
	else
		error = EBADF;

	MUTEX_RELEASE(&proc->fdmutex);
	return error;
}

int fd_getflags(int fd, int *flags) {
	int error = 0;
	proc_t *proc = _cpu()->thread->proc;
	MUTEX_ACQUIRE(&proc->fdmutex, false);

	if (proc->fd[fd].file)
		*flags = proc->fd[fd].flags;
	else
		error = EBADF;

	MUTEX_RELEASE(&proc->fdmutex);
	return error;
}

void fd_release(file_t *file) {
	FILE_RELEASE(file);
}

int fd_new(int flags, file_t **rfile, int *rfd) {
	file_t *file = newfile();
	if (file == NULL)
		return ENOMEM;

	proc_t *proc = _cpu()->thread->proc;
	MUTEX_ACQUIRE(&proc->fdmutex, false);

	// find first free fd
	int fd = getfree(proc->fdfirst);

	// resize table if not found
	if (fd == -1) {
		fd = proc->fdcount;
		int error = growtable(fd + 1);
		if (error) {
			MUTEX_RELEASE(&proc->fdmutex);
			cleanfile(file);
			return error;
		}
	}

	proc->fdfirst = fd + 1;
	proc->fd[fd].file = file;
	proc->fd[fd].flags = flags;

	MUTEX_RELEASE(&proc->fdmutex);

	*rfile = file;
	*rfd = fd;
	return 0;
}

int fd_close(int fd) {
	proc_t *proc = _cpu()->thread->proc;
	MUTEX_ACQUIRE(&proc->fdmutex, false);

	file_t *file = fd < proc->fdcount ? proc->fd[fd].file : NULL;
	if (file) {
		proc->fd[fd].file = NULL;
		if (proc->fdfirst > fd)
			proc->fdfirst = fd;
	}

	MUTEX_RELEASE(&proc->fdmutex);

	if (file) {
		FILE_RELEASE(file);
	}
	else {
		return EBADF;
	}

	return 0;
}

int fd_clone(proc_t *targproc) {
	proc_t *proc = _cpu()->thread->proc;
	int error = 0;
	MUTEX_ACQUIRE(&proc->fdmutex, false);

	targproc->fdcount = proc->fdcount;
	targproc->fdfirst = proc->fdfirst;
	targproc->fd = alloc(proc->fdcount * sizeof(fd_t));
	if (targproc->fd == NULL) {
		error = ENOMEM;
		goto cleanup;
	}

	for (int i = 0; i < targproc->fdcount; ++i) {
		if (proc->fd[i].file == NULL)
			continue;

		targproc->fd[i] = proc->fd[i];
		FILE_HOLD(targproc->fd[i].file);
	}

	cleanup:
	MUTEX_RELEASE(&proc->fdmutex);
	return error;
}

int fd_dup(int oldfd, int newfd, bool exact, int fdflags, int *retfd) {
	proc_t *proc = _cpu()->thread->proc;
	int err = 0;

	if (newfd < 0 || newfd >= FDTABLE_LIMIT)
		return EBADF;

	MUTEX_ACQUIRE(&proc->fdmutex, false);

	file_t *file = oldfd < proc->fdcount ? proc->fd[oldfd].file : NULL;
	if (file == NULL) {
		err = EBADF;
		goto cleanup;
	}

	if (exact) {
		if (newfd >= proc->fdcount) {
			err = growtable(newfd + 1);
			if (err)
				goto cleanup;
		}

		if (proc->fd[newfd].file)
			FILE_RELEASE(proc->fd[newfd].file);

		proc->fd[newfd].file = file;
		proc->fd[newfd].flags = fdflags;
		*retfd = newfd;
	} else {
		int fd = getfree(newfd);

		if (fd == -1) {
			size_t newsize = newfd < proc->fdcount ? proc->fdcount + 1 : newfd;
			fd = newsize - 1;
			err = growtable(newsize);
			if (err)
				goto cleanup;
		}

		proc->fd[fd].file = file;
		proc->fd[fd].flags = fdflags;
		*retfd = fd;
	}

	FILE_HOLD(file);

	cleanup:
	MUTEX_RELEASE(&proc->fdmutex);
	return err;
}
