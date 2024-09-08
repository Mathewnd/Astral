#include <kernel/vfs.h>
#include <kernel/pipefs.h>
#include <kernel/slab.h>
#include <kernel/alloc.h>
#include <kernel/timekeeper.h>
#include <kernel/vmm.h>
#include <logging.h>
#include <errno.h>
#include <kernel/abi.h>
#include <kernel/poll.h>

#define BUFFER_SIZE 16 * PAGE_SIZE
#define PIPE_ATOMIC_SIZE 4096

static scache_t *nodecache;
static uintmax_t currentinode;

static vops_t vnops;

#define INTERNAL_LOCK(v) MUTEX_ACQUIRE(&(v)->lock, false)
#define INTERNAL_UNLOCK(v) MUTEX_RELEASE(&(v)->lock)

int pipefs_close(vnode_t *node, int flags, cred_t *cred) {
	INTERNAL_LOCK(node);
	pipenode_t *pipenode = (pipenode_t *)node;

	if (flags & V_FFLAGS_READ) {
		--pipenode->readers;
	}

	if (flags & V_FFLAGS_WRITE) {
		--pipenode->writers;
	}

	if (pipenode->readers == 0)
		poll_event(&pipenode->pollheader, POLLERR);

	if (pipenode->writers == 0)
		poll_event(&pipenode->pollheader, POLLHUP);

	INTERNAL_UNLOCK(node);
	return 0;
}

int pipefs_open(vnode_t **node, int flags, cred_t *cred) {
	if ((flags & (V_FFLAGS_WRITE | V_FFLAGS_READ)) == 0)
		return EINVAL;

	INTERNAL_LOCK(*node);
	pipenode_t *pipenode = (pipenode_t *)(*node);

	if (flags & V_FFLAGS_READ) {
		++pipenode->readers;
	}

	if (flags & V_FFLAGS_WRITE) {
		++pipenode->writers;
	}

	int error = 0;

	if (pipenode->writers > 0 && pipenode->readers > 0)
		goto leave;

	if (flags & V_FFLAGS_NONBLOCKING) {
		if (flags & V_FFLAGS_READ) {
			goto leave;
		} else if (flags & V_FFLAGS_WRITE) {
			error = ENXIO;
			goto leave;
		}
	}

	// from this point on, flags has only one of V_FFLAGS_READ or V_FFLAGS_WRITE
	// and there are either no writers or no readers.
	eventlistener_t listener;
	EVENT_INITLISTENER(&listener);
	if (flags & V_FFLAGS_READ) {
		EVENT_ATTACH(&listener, &pipenode->writeopenevent);
	} else {
		EVENT_ATTACH(&listener, &pipenode->readopenevent);
	}

	INTERNAL_UNLOCK(*node);
	error = EVENT_WAIT(&listener, 0);
	INTERNAL_LOCK(*node);

	EVENT_DETACHALL(&listener);

	leave:
	if (error) {
		INTERNAL_UNLOCK(*node);
		pipefs_close(*node, flags, cred);
	} else {
		if (flags & V_FFLAGS_READ)
			EVENT_SIGNAL(&pipenode->readopenevent);
		else
			EVENT_SIGNAL(&pipenode->writeopenevent);
		INTERNAL_UNLOCK(*node);
	}

	return error;
}

static int internalpoll(vnode_t *node, polldata_t *data, int events) {
	pipenode_t *pipenode = (pipenode_t *)node;
	int revents = 0;

	if (events & POLLIN) {
		events |= POLLHUP;
		if (pipenode->writers == 0)
			revents |= POLLHUP;
		else if (RINGBUFFER_DATACOUNT(&pipenode->data) > 0)
			revents |= POLLIN;
	}

	if (events & POLLOUT) {
		events |= POLLERR;
		if (pipenode->readers == 0)
			revents |= POLLERR;
		// poll will only return POLLOUT if an atomic write can be done without blocking
		// this is undocumented in POSIX but many unices implement it like this
		else if (RINGBUFFER_DATACOUNT(&pipenode->data) < BUFFER_SIZE - PIPE_ATOMIC_SIZE)
			revents |= POLLOUT;
	}

	if (revents == 0 && data)
		poll_add(&pipenode->pollheader, data, events);

	return revents;
}

int pipefs_read(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc, cred_t *cred) {
	pipenode_t *pipenode = (pipenode_t *)node;
	INTERNAL_LOCK(node);

	int error = 0;
	*readc = 0;

	// wait until there is data to read or return if nonblock
	while (1) {
		// check for available data or if the write end has been closed
		int revents = internalpoll(node, NULL, POLLIN);
		if (revents)
			break;

		if (flags & V_FFLAGS_NONBLOCKING) {
			error = EAGAIN;
			goto leave;
		}

		// do the wait
		polldesc_t desc = {0};
		error = poll_initdesc(&desc, 1);
		if (error)
			goto leave;

		revents = internalpoll(node, &desc.data[0], POLLIN);

		INTERNAL_UNLOCK(node);

		error = poll_dowait(&desc, 0);

		poll_leave(&desc);
		poll_destroydesc(&desc);

		if (error)
			return error;

		INTERNAL_LOCK(node);
	}

	*readc = ringbuffer_read(&pipenode->data, buffer, size);

	// signal that there is space to write for any threads blocked on this pipe
	if (RINGBUFFER_DATACOUNT(&pipenode->data) < BUFFER_SIZE - PIPE_ATOMIC_SIZE)
		poll_event(&pipenode->pollheader, POLLOUT);

	leave:
	INTERNAL_UNLOCK(node);
	return error;
}

static int writetopipe(pipenode_t *pipenode, void *buffer, size_t size, size_t *writec) {
	if (pipenode->readers == 0) {
		if (_cpu()->thread->proc)
			signal_signalproc(_cpu()->thread->proc, SIGPIPE);

		return EPIPE;
	}

	*writec = ringbuffer_write(&pipenode->data, buffer, size);

	__assert(*writec);
	poll_event(&pipenode->pollheader, POLLIN);
	return 0;
}

int pipefs_write(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec, cred_t *cred) {
	pipenode_t *pipenode = (pipenode_t *)node;
	INTERNAL_LOCK(node);

	int error = 0;
	*writec = 0;

	// there are 4 cases:
	// 1. blocking and n <= PIPE_ATOMIC_SIZE
	// 	atomic write, will only return once ALL the bytes can be written.
	// 2. nonblocking and n <= PIPE_ATOMIC_SIZE
	//	if there is no room to write everything, fail with EAGAIN
	// 3. blocking and > PIPE_ATOMIC_SIZE
	// 	will only return when everything is written or a signal is caught, but is not guaranteed to be atomic.
	// 4. nonblocking and > PIPE_ATOMIC_SIZE
	// 	if pipe is full, fail with EAGAIN. otherwise, do a partial write.

	bool atomic = size <= PIPE_ATOMIC_SIZE;
	bool nonblock = flags & V_FFLAGS_NONBLOCKING;
	size_t case3written = 0;

	while (1) {
		// check for space or if the read end has been closed
		int revents = internalpoll(node, NULL, POLLOUT);
		if (revents & POLLHUP)
			break;

		size_t freebytes = RINGBUFFER_FREESPACE(&pipenode->data);
		// case 1
		if (nonblock == false && atomic && freebytes >= size)
			break;

		// case 2
		if (nonblock && atomic && freebytes < size) {
			// if not enough space
			error = EAGAIN;
			goto leave;
		} else if (nonblock && atomic) {
			// write
			break;
		}

		// case 3
		if (nonblock == false && atomic == false && freebytes) {
			size_t tmp;
			size_t writecount = min(freebytes, size - case3written);
			error = writetopipe(pipenode, (void *)((uintptr_t)buffer + case3written), writecount, &tmp);
			if (error)
				goto leave;

			case3written += writecount;
			__assert(tmp == writecount);

			if (case3written == size) {
				*writec = size;
				goto leave;
			}
		}

		if (nonblock && atomic == false && freebytes != 0) {
			// case 4 non not full pipe
			break;
		} else if (nonblock && atomic == false) {
			// case 4 full pipe
			error = EAGAIN;
			goto leave;
		}

		// do the wait
		polldesc_t desc = {0};
		error = poll_initdesc(&desc, 1);
		if (error)
			goto leave;

		revents = internalpoll(node, &desc.data[0], POLLOUT);

		INTERNAL_UNLOCK(node);

		error = poll_dowait(&desc, 0);

		poll_leave(&desc);
		poll_destroydesc(&desc);

		if (error)
			return error;

		INTERNAL_LOCK(node);
	}

	error = writetopipe(pipenode, buffer, size, writec);

	leave:
	INTERNAL_UNLOCK(node);
	return error;
}

int pipefs_poll(vnode_t *node, polldata_t *data, int events) {
	int revents = 0;
	INTERNAL_LOCK(node);

	revents = internalpoll(node, data, events);

	INTERNAL_UNLOCK(node);
	return revents;
}

int pipefs_getattr(vnode_t *node, vattr_t *attr, cred_t *cred) {
	pipenode_t *pipenode = (pipenode_t *)node;

	INTERNAL_LOCK(node);
	*attr = pipenode->attr;
	attr->type = node->type;
	INTERNAL_UNLOCK(node);

	return 0;
}

int pipefs_setattr(vnode_t *node, vattr_t *attr, int which, cred_t *cred) {
	pipenode_t *pipenode = (pipenode_t *)node;

	INTERNAL_LOCK(node);
	if (which & V_ATTR_GID)
		pipenode->attr.gid = attr->gid;
	if (which & V_ATTR_UID)
		pipenode->attr.uid = attr->uid;
	if (which & V_ATTR_MODE)
		pipenode->attr.mode = attr->mode;
	if (which & V_ATTR_ATIME)
		pipenode->attr.atime = attr->atime;
	if (which & V_ATTR_MTIME)
		pipenode->attr.mtime = attr->mtime;
	if (which & V_ATTR_CTIME)
		pipenode->attr.ctime = attr->ctime;
	INTERNAL_UNLOCK(node);
	return 0;
}

int pipefs_inactive(vnode_t *node) {
	pipenode_t *pipenode = (pipenode_t *)node;
	INTERNAL_LOCK(node);
	ringbuffer_destroy(&pipenode->data);
	slab_free(nodecache, node);
	return 0;
}

// the mutex vnode is handled internally
// (as we can sleep when waiting for data/space)
// this does mean that operations are not atomic between a VOP_LOCK and VOP_UNLOCK
static int pipefs_lock(vnode_t *vnode) {
	return 0;
}

static int pipefs_unlock(vnode_t *vnode) {
	return 0;
}

static int pipefs_enodev() {
	return ENODEV;
}

static vops_t vnops = {
	.create = pipefs_enodev,
	.open = pipefs_open,
	.close = pipefs_close,
	.getattr = pipefs_getattr,
	.setattr = pipefs_setattr,
	.lookup = pipefs_enodev,
	.poll = pipefs_poll,
	.read = pipefs_read,
	.write = pipefs_write,
	.access = pipefs_enodev,
	.unlink = pipefs_enodev,
	.link = pipefs_enodev,
	.symlink = pipefs_enodev,
	.readlink = pipefs_enodev,
	.inactive = pipefs_inactive,
	.mmap = pipefs_enodev,
	.munmap = pipefs_enodev,
	.getdents = pipefs_enodev,
	.resize = pipefs_enodev,
	.rename = pipefs_enodev,
	.putpage = pipefs_enodev,
	.getpage = pipefs_enodev,
	.sync = pipefs_enodev,
	.lock = pipefs_lock,
	.unlock = pipefs_unlock
};

static void ctor(scache_t *cache, void *obj) {
	pipenode_t *node = obj;
	memset(node, 0, sizeof(pipenode_t));
	VOP_INIT(&node->vnode, &vnops, 0, V_TYPE_FIFO, NULL);
	node->attr.inode = ++currentinode;
	POLL_INITHEADER(&node->pollheader);
	EVENT_INITHEADER(&node->writeopenevent);
	EVENT_INITHEADER(&node->readopenevent);
}

void pipefs_init() {
	nodecache = slab_newcache(sizeof(pipenode_t), 0, ctor, ctor);
	__assert(nodecache);
}

int pipefs_newpipe(vnode_t **nodep) {
	__assert(_cpu()->intstatus);
	vnode_t *vnode = slab_allocate(nodecache);
	pipenode_t *node = (pipenode_t *)vnode;
	if (node == NULL)
		return ENOMEM;

	int e = ringbuffer_init(&node->data, BUFFER_SIZE);
	if (e) {
		VOP_RELEASE(vnode);
		return e;
	}

	*nodep = vnode;

	return 0;
}

// assumes node is locked upon calling
int pipefs_getbinding(vnode_t *node, vnode_t **pipep) {
	__assert(node->type == V_TYPE_FIFO);

	if (node->fifobinding) {
		*pipep = node->fifobinding;
		VOP_HOLD(*pipep);
		return 0;
	}

	int error = pipefs_newpipe(pipep);
	if (error)
		return error;

	VOP_HOLD(*pipep);
	node->fifobinding = *pipep;
	return error;
}

void pipefs_leavebinding(vnode_t *pipenode) {
	VOP_RELEASE(pipenode);
}
