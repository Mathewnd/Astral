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
	vfs_close(file->vnode, fileflagstovnodeflags(file->flags));
	VOP_RELEASE(file->vnode);
	slab_free(filecache, file);
}

file_t *fd_allocate() {
	return newfile();
}

file_t *fd_get(int fd) {
	proc_t *proc = _cpu()->thread->proc;
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->fdlock);

	file_t *file = fd < proc->fdcount ? proc->fd[fd].file : NULL;
	if (file)
		FILE_HOLD(file);

	spinlock_release(&proc->fdlock);
	interrupt_set(intstatus);

	if (file)
		MUTEX_ACQUIRE(&file->mutex, false);

	return file;
}

void fd_release(file_t *file) {
	MUTEX_RELEASE(&file->mutex);
	FILE_RELEASE(file);
}

int fd_new(int flags, file_t **rfile, int *rfd) {
	file_t *file = newfile();
	if (file == NULL)
		return ENOMEM;

	proc_t *proc = _cpu()->thread->proc;
	spinlock_acquire(&proc->fdlock);

	// find first free fd
	int fd = -1;
	int sfd;
	for (sfd = proc->fdfirst; fd < proc->fdcount && proc->fd[fd].file; ++fd);

	if (sfd != proc->fdcount)
		fd = sfd;

	// resize table if not found
	if (fd == -1) {
		void *newtable = realloc(proc->fd, sizeof(fd_t) * (proc->fdcount + 1));
		if (newtable == NULL) {
			spinlock_release(&proc->fdlock);
			cleanfile(file);
			return ENOMEM;
		}

		proc->fd = newtable;
		fd = proc->fdcount;
		++proc->fdcount;
	}

	proc->fdfirst = fd + 1;
	proc->fd[fd].file = file;
	proc->fd[fd].flags = flags;
	MUTEX_ACQUIRE(&file->mutex, false);
	FILE_HOLD(file);

	spinlock_release(&proc->fdlock);

	*rfile = file;
	*rfd = fd;
	return 0;
}

int fd_close(int fd) {
	proc_t *proc = _cpu()->thread->proc;
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->fdlock);

	file_t *file = fd < proc->fdcount ? proc->fd[fd].file : NULL;
	if (file) {
		proc->fd[fd].file = NULL;
		proc->fdfirst = fd;
	}

	spinlock_release(&proc->fdlock);
	interrupt_set(intstatus);

	if (file) {
		FILE_RELEASE(file);
	}
	else {
		return EBADF;
	}

	return 0;
}
