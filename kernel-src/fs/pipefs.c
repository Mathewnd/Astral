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

static scache_t *nodecache;
static uintmax_t currentinode;

static vops_t vnops;

int pipefs_open(vnode_t **node, int flags, cred_t *cred) {
	VOP_LOCK(*node);

	pipenode_t *pipenode = (pipenode_t *)(*node);

	if (flags & V_FFLAGS_READ) {
		++pipenode->readers;
	}

	if (flags & V_FFLAGS_WRITE) {
		++pipenode->writers;
	}

	VOP_UNLOCK(*node);
	return 0;
}

int pipefs_close(vnode_t *node, int flags, cred_t *cred) {
	VOP_LOCK(node);
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

	VOP_UNLOCK(node);
	return 0;
}

// TODO for pipefs_read and pipefs_write, the return actually depends on how much data is in the pipe
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
		else if (RINGBUFFER_DATACOUNT(&pipenode->data) < BUFFER_SIZE)
			revents |= POLLOUT;
	}

	if (revents == 0 && data)
		poll_add(&pipenode->pollheader, data, events);

	return revents;
}

int pipefs_read(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc, cred_t *cred) {
	pipenode_t *pipenode = (pipenode_t *)node;
	VOP_LOCK(node);

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

		VOP_UNLOCK(node);

		error = poll_dowait(&desc, 0);

		poll_leave(&desc);
		poll_destroydesc(&desc);

		if (error)
			return error;

		VOP_LOCK(node);
	}

	*readc = ringbuffer_read(&pipenode->data, buffer, size);

	// signal that there is space to write for any threads blocked on this pipe
	if (*readc)
		poll_event(&pipenode->pollheader, POLLOUT);

	leave:
	VOP_UNLOCK(node);
	return error;
}

int pipefs_write(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec, cred_t *cred) {
	pipenode_t *pipenode = (pipenode_t *)node;
	VOP_LOCK(node);

	int error = 0;
	*writec = 0;

	// wait until there is space to write data or return if nonblock
	while (1) {
		// check for space or if the read end has been closed
		int revents = internalpoll(node, NULL, POLLOUT);
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

		revents = internalpoll(node, &desc.data[0], POLLOUT);

		VOP_UNLOCK(node);

		error = poll_dowait(&desc, 0);

		poll_leave(&desc);
		poll_destroydesc(&desc);

		if (error)
			return error;

		VOP_LOCK(node);
	}

	if (pipenode->readers == 0) {
		if (_cpu()->thread->proc)
			signal_signalproc(_cpu()->thread->proc, SIGPIPE);

		error = EPIPE;
		goto leave;
	}

	*writec = ringbuffer_write(&pipenode->data, buffer, size);

	__assert(*writec);
	poll_event(&pipenode->pollheader, POLLIN);

	leave:
	VOP_UNLOCK(node);
	return error;
}

int pipefs_poll(vnode_t *node, polldata_t *data, int events) {
	int revents = 0;
	VOP_LOCK(node);

	revents = internalpoll(node, data, events);

	VOP_UNLOCK(node);
	return revents;
}

int pipefs_getattr(vnode_t *node, vattr_t *attr, cred_t *cred) {
	pipenode_t *pipenode = (pipenode_t *)node;

	VOP_LOCK(node);
	*attr = pipenode->attr;
	attr->type = node->type;
	VOP_UNLOCK(node);

	return 0;
}

int pipefs_setattr(vnode_t *node, vattr_t *attr, int which, cred_t *cred) {
	pipenode_t *pipenode = (pipenode_t *)node;

	VOP_LOCK(node);
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
	VOP_UNLOCK(node);
	return 0;
}

int pipefs_inactive(vnode_t *node) {
	pipenode_t *pipenode = (pipenode_t *)node;
	VOP_LOCK(node);
	ringbuffer_destroy(&pipenode->data);
	slab_free(nodecache, node);
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
	.sync = pipefs_enodev
};

static void ctor(scache_t *cache, void *obj) {
	pipenode_t *node = obj;
	memset(node, 0, sizeof(pipenode_t));
	VOP_INIT(&node->vnode, &vnops, 0, V_TYPE_FIFO, NULL);
	node->attr.inode = ++currentinode;
	POLL_INITHEADER(&node->pollheader);
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
