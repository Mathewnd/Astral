#include <kernel/vfs.h>
#include <kernel/slab.h>
#include <logging.h>
#include <errno.h>
#include <kernel/poll.h>
#include <kernel/init.h>
#include <kernel/eventfs.h>

#define EVENT_COUNTER_MAX 0xfffffffffffffffelu

typedef struct {
	vnode_t vnode;
	pollheader_t pollheader;
	uint64_t counter;
	bool semaphore;
} event_node_t;

static scache_t *node_cache;

int eventfs_close(vnode_t *node, int flags, cred_t *cred) {
	return 0; // not needed
}

static int internal_poll(event_node_t *event_node, polldata_t *data, int events, size_t write_limit) {
	int revents = 0;

	if ((events & POLLIN) && event_node->counter)
		revents |= POLLIN;

	if ((events & POLLOUT) && event_node->counter <= write_limit)
		revents |= POLLOUT;

	if (data && !revents)
		poll_add(&event_node->pollheader, data, events);

	return revents;
}

static int internal_poll_wait(event_node_t *event_node, int event, bool non_blocking, size_t write_limit) {
	for (;;) {
		polldesc_t desc = {0};
		int error = poll_initdesc(&desc, 1);
		if (error)
			return error;

		int revents = internal_poll(event_node, &desc.data[0], event, write_limit);
		if (revents) {
			poll_leave(&desc);
			poll_destroydesc(&desc);
			break;
		}

		if (non_blocking) {
			poll_leave(&desc);
			poll_destroydesc(&desc);
			return EAGAIN;
		}

		MUTEX_RELEASE(&event_node->vnode.lock);

		error = poll_dowait(&desc, 0);

		poll_leave(&desc);
		poll_destroydesc(&desc);

		MUTEX_ACQUIRE(&event_node->vnode.lock);
		if (error)
			return error;
	}

	return 0;
}

int eventfs_read(vnode_t *node, iovec_iterator_t *iovec_iterator, size_t size, uintmax_t offset, int flags, size_t *bytes_read, cred_t *cred) {
	event_node_t *event_node = (event_node_t *)node;

	if (size < 8)
		return EINVAL;

	MUTEX_ACQUIRE(&node->lock);

	int error = internal_poll_wait(event_node, POLLIN, flags & V_FFLAGS_NONBLOCKING, 0);
	if (error)
		goto leave;

	uint64_t return_value = event_node->semaphore ? 1 : event_node->counter;	
	error = iovec_iterator_copy_from_buffer(iovec_iterator, &return_value, 8);
	if (error)
		goto leave;

	event_node->counter = event_node->semaphore ? (event_node->counter - 1) : 0;
	*bytes_read = 8;

	poll_event(&event_node->pollheader, POLLOUT);

	leave:
	MUTEX_RELEASE(&node->lock);
	return error;
}

int eventfs_write(vnode_t *node, iovec_iterator_t *iovec_iterator, size_t size, uintmax_t offset, int flags, size_t *bytes_written, cred_t *cred) {
	event_node_t *event_node = (event_node_t *)node;

	if (size < 8)
		return EINVAL;

	uint64_t value_to_write;
	int error = iovec_iterator_copy_to_buffer(iovec_iterator, &value_to_write, 8);
	if (error)
		return error;

	if (value_to_write > EVENT_COUNTER_MAX)
		return EINVAL;

	MUTEX_ACQUIRE(&node->lock);

	error = internal_poll_wait(event_node, POLLOUT, flags & V_FFLAGS_NONBLOCKING, EVENT_COUNTER_MAX - value_to_write);
	if (error)
		goto leave;

	event_node->counter += value_to_write;
	__assert(event_node->counter <= EVENT_COUNTER_MAX);

	poll_event(&event_node->pollheader, POLLIN);
	*bytes_written = 8;

	leave:
	MUTEX_RELEASE(&node->lock);
	return error;
}

int eventfs_poll(vnode_t *node, polldata_t *data, int events) {
	MUTEX_ACQUIRE(&node->lock);
	int revents = internal_poll((event_node_t *)node, data, events, EVENT_COUNTER_MAX - 1);
	MUTEX_RELEASE(&node->lock);
	return revents;
}

static vops_t vnops;

int eventfs_inactive(vnode_t *node) {
	event_node_t *event_node = (event_node_t *)node;
	VOP_INIT(&event_node->vnode, &vnops, 0, V_TYPE_CHDEV, NULL);
	POLL_INITHEADER(&event_node->pollheader);
	slab_free(node_cache, node);
	return 0;
}

static int eventfs_lock(vnode_t *) {
	return 0;
}

static int eventfs_unlock(vnode_t *) {
	return 0;
}

static int eventfs_advlock(vnode_t *node, int op, advlock_t *advlock) {
	return vfs_advlock(node, op, advlock);
}

static int eventfs_enodev() {
	return ENODEV;
}

static vops_t vnops = {
	.create = eventfs_enodev,
	.open = eventfs_enodev,
	.close = eventfs_close,
	.getattr = eventfs_enodev,
	.setattr = eventfs_enodev,
	.lookup = eventfs_enodev,
	.poll = eventfs_poll,
	.read = eventfs_read,
	.write = eventfs_write,
	.access = eventfs_enodev,
	.unlink = eventfs_enodev,
	.link = eventfs_enodev,
	.symlink = eventfs_enodev,
	.readlink = eventfs_enodev,
	.inactive = eventfs_inactive,
	.mmap = eventfs_enodev,
	.munmap = eventfs_enodev,
	.getdents = eventfs_enodev,
	.resize = eventfs_enodev,
	.rename = eventfs_enodev,
	.ioctl = eventfs_enodev,
	.putpage = eventfs_enodev,
	.getpage = eventfs_enodev,
	.sync = eventfs_enodev,
	.advlock = eventfs_advlock,
	.lock = eventfs_lock,
	.unlock = eventfs_unlock
};

static bool ctor(scache_t *cache, void *obj) {
	event_node_t *node = obj;
	VOP_INIT(&node->vnode, &vnops, 0, V_TYPE_CHDEV, NULL);
	POLL_INITHEADER(&node->pollheader);
	return true;
}

void eventfs_init() {
	node_cache = slab_newcache(sizeof(event_node_t), 0, ctor, NULL);
	__assert(node_cache);
}

INIT_ROUTINE_DEFINE(eventfs, INIT_ROUTINE_FLAGS_NONE, eventfs_init, bsp_early);

int eventfs_create_node(vnode_t **vnodep, size_t initval, bool semaphore) {
	event_node_t *event_node = slab_allocate(node_cache);
	if (event_node == NULL)
		return ENOMEM;

	event_node->counter = initval;
	event_node->semaphore = semaphore;

	*vnodep = &event_node->vnode;
	return 0;
}
