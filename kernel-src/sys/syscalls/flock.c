#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/vfs.h>

syscallret_t syscall_flock(context_t *, int fd, int op_flag) {
	syscallret_t ret = {
		.ret = -1
	};

	int op = op_flag &= ~ADVLOCK_NON_BLOCKING;
	switch (op) {
		case ADVLOCK_UNLOCK:
		case ADVLOCK_SHARED:
		case ADVLOCK_EXCLUSIVE:
			break;
		default:
			ret.errno = EINVAL;
			return ret;
	}

	file_t *file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		return ret;
	}

	MUTEX_ACQUIRE(&file->mutex);

	advlock_t *advlock = file->advlock;
	if (advlock)
		ADVLOCK_REF(advlock);

	if (op == ADVLOCK_UNLOCK && advlock == NULL) {
		// trying to unlock a lock which does not exist, just leave
		ret.errno = 0;
		ret.ret = 0;
		goto leave;
	}

	if (op != ADVLOCK_UNLOCK && advlock == NULL) {
		// locking and the lock does not exist yet, allocate it
		advlock = advlock_allocate();
		if (advlock == NULL) {
			ret.errno = ENOMEM;
			goto leave;
		}

		file->advlock = advlock;
		advlock->refcount = 2;
	}

	vnode_t *vn = file->vnode;
	VOP_HOLD(vn);

	// release the file so that it can be closed etc in case we block
	MUTEX_RELEASE(&file->mutex);
	fd_release(file);

	ret.errno = VOP_ADVLOCK(vn, op_flag, advlock);
	ret.ret = ret.errno ? -1 : 0;

	VOP_RELEASE(vn);
	ADVLOCK_UNREF(advlock);

	return ret;

	leave:
	if (advlock)
		ADVLOCK_UNREF(advlock);
	MUTEX_RELEASE(&file->mutex);
	fd_release(file);

	return ret;
}
