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
		event_signal(&pipenode->readevent);

	if (pipenode->writers == 0)
		event_signal(&pipenode->writeevent);

	VOP_UNLOCK(node);
	return 0;
}

int pipefs_read(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc, cred_t *cred) {
	pipenode_t *pipenode = (pipenode_t *)node;
	VOP_LOCK(node);

	int error = 0;
	*readc = 0;
	size_t datacount = 0;

	// wait until there is data to read or return if nonblock
	while (datacount == 0) {
		datacount = RINGBUFFER_DATACOUNT(&pipenode->data);
		if (datacount == 0) {
			if (pipenode->writers == 0)
				goto leave;

			if (flags & V_FFLAGS_NONBLOCKING) {
				error = EAGAIN;
				goto leave;
			}

			// to make it harder for a race condition to happen TODO figure out a way to fix this
			bool status = interrupt_set(false);
			VOP_UNLOCK(node);

			error = event_wait(&pipenode->writeevent, true);
			interrupt_set(status);
			if (error)
				return error;

			VOP_LOCK(node);
		}
	}

	*readc = ringbuffer_read(&pipenode->data, buffer, size);

	if (*readc < datacount)
		event_signal(&pipenode->writeevent);

	event_signal(&pipenode->readevent);

	leave:
	VOP_UNLOCK(node);
	return error;
}

int pipefs_write(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec, cred_t *cred) {
	pipenode_t *pipenode = (pipenode_t *)node;
	VOP_LOCK(node);

	int error = 0;
	*writec = 0;

	// wait until there is space to write data into or return if nonblock
	size_t datacount = BUFFER_SIZE;
	while (datacount == BUFFER_SIZE && pipenode->readers) {
		datacount = RINGBUFFER_DATACOUNT(&pipenode->data);
		if (datacount == BUFFER_SIZE) {
			if (flags & V_FFLAGS_NONBLOCKING) {
				error = EAGAIN;
				goto leave;
			}

			// to make it harder for a race condition to happen TODO figure out a way to fix this
			bool status = interrupt_set(false);
			VOP_UNLOCK(node);

			error = event_wait(&pipenode->readevent, true);
			interrupt_set(status);
			if (error)
				return error;

			VOP_LOCK(node);
		}
	}

	if (pipenode->readers == 0) {
		// TODO throw SIGPIPE
		error = EPIPE;
		goto leave;
	}

	*writec = ringbuffer_write(&pipenode->data, buffer, size);

	if (datacount + *writec < BUFFER_SIZE)
		event_signal(&pipenode->readevent);

	event_signal(&pipenode->writeevent);

	leave:
	VOP_UNLOCK(node);
	return error;
}

int pipefs_poll(vnode_t *node, polldata_t *data, int events) {
	pipenode_t *pipenode = (pipenode_t *)node;
	int revents = 0;
	VOP_LOCK(node);

 	if (events & POLLIN) {
		if (pipenode->writers == 0)
			revents |= POLLHUP;
		else if (RINGBUFFER_DATACOUNT(&pipenode->data) > 0)
			revents |= POLLIN;
	}

	if (events & POLLOUT) {
		if (pipenode->readers == 0)
			revents |= POLLERR;
		else if (RINGBUFFER_DATACOUNT(&pipenode->data) < BUFFER_SIZE)
			revents |= POLLOUT;
	}

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

int pipefs_setattr(vnode_t *node, vattr_t *attr, cred_t *cred) {
	pipenode_t *pipenode = (pipenode_t *)node;

	VOP_LOCK(node);
	pipenode->attr.gid = attr->gid;
	pipenode->attr.uid = attr->uid;
	pipenode->attr.mode = attr->mode;
	pipenode->attr.atime = attr->atime;
	pipenode->attr.mtime = attr->mtime;
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
	.resize = pipefs_enodev
};

static void ctor(scache_t *cache, void *obj) {
	pipenode_t *node = obj;
	memset(node, 0, sizeof(pipenode_t));
	VOP_INIT(&node->vnode, &vnops, 0, 0, NULL);
	node->attr.inode = ++currentinode;
	EVENT_INIT(&node->writeevent);
	EVENT_INIT(&node->readevent);
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
