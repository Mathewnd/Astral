#include <kernel/vfs.h>
#include <kernel/slab.h>
#include <kernel/alloc.h>
#include <kernel/timekeeper.h>
#include <kernel/vmm.h>
#include <logging.h>
#include <errno.h>
#include <kernel/abi.h>
#include <kernel/poll.h>
#include <kernel/sock.h>
#include <kernel/auth.h>

static scache_t *nodecache;
static uintmax_t currentinode;

#define INTERNAL_LOCK(v) MUTEX_ACQUIRE(&(v)->lock, false)
#define INTERNAL_UNLOCK(v) MUTEX_RELEASE(&(v)->lock)

int sockfs_open(vnode_t **node, int flags, cred_t *cred) {
	return ENOSYS; // not needed
}

int sockfs_close(vnode_t *node, int flags, cred_t *cred) {
	return ENOSYS; // not needed
}

int sockfs_read(vnode_t *node, iovec_iterator_t *iovec_iterator, size_t size, uintmax_t offset, int flags, size_t *bytes_read, cred_t *cred) {
	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(node);
	sockdesc_t desc = {
		.addr = NULL,
		.iovec_iterator = iovec_iterator,
		.count = size,
		.flags = flags,
		.donecount = 0,
		.ctrl = NULL,
		.ctrllen = 0
	};

	int e = socket->ops->recv(socket, &desc);

	*bytes_read = desc.donecount;

	return e;
}

int sockfs_write(vnode_t *node, iovec_iterator_t *iovec_iterator, size_t size, uintmax_t offset, int flags, size_t *bytes_written, cred_t *cred) {
	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(node);
	sockdesc_t desc = {
		.addr = NULL,
		.iovec_iterator = iovec_iterator,
		.count = size,
		.flags = flags,
		.donecount = 0,
		.ctrl = NULL,
		.ctrllen = 0
	};

	int e = socket->ops->send(socket, &desc);

	*bytes_written = desc.donecount;

	return e;
}

int sockfs_poll(vnode_t *node, polldata_t *data, int events) {
	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(node);
	return socket->ops->poll(socket, data, events);
}

int sockfs_getattr(vnode_t *node, vattr_t *attr, cred_t *cred) {
	socketnode_t *socketnode = (socketnode_t *)node;

	INTERNAL_LOCK(node);
	*attr = socketnode->attr;
	attr->type = node->type;
	INTERNAL_UNLOCK(node);

	return 0;
}

int sockfs_setattr(vnode_t *node, vattr_t *attr, int which, cred_t *cred) {
	socketnode_t *socketnode = (socketnode_t *)node;
	INTERNAL_LOCK(node);

	if (which & V_ATTR_GID)
		socketnode->attr.gid = attr->gid;
	if (which & V_ATTR_UID)
		socketnode->attr.uid = attr->uid;
	if (which & V_ATTR_MODE)
		socketnode->attr.mode = attr->mode;
	if (which & V_ATTR_ATIME)
		socketnode->attr.atime = attr->atime;
	if (which & V_ATTR_MTIME)
		socketnode->attr.mtime = attr->mtime;
	if (which & V_ATTR_CTIME)
		socketnode->attr.ctime = attr->ctime;

	INTERNAL_UNLOCK(node);
	return 0;
}

// arg can be in userspace
int sockfs_ioctl(vnode_t *node, unsigned long request, void *arg, int *result, cred_t *cred) {
	switch (request) {
		case SIOCSIFADDR: {
			ifreq_t ifreq;
			int e = USERCOPY_POSSIBLY_FROM_USER(&ifreq, arg, sizeof(ifreq_t));
			if (e)
				return e;

			netdev_t *netdev = netdev_getdev(ifreq.name);
			if (netdev) {
				e = auth_network_check(cred, AUTH_ACTIONS_NETWORK_CONFIGURE, NULL, netdev);
				if (e)
					return e;

				sockaddr_t sockaddr;
				e = sock_convertaddress(&sockaddr, &ifreq.addr);
				if (e)
					return e;
				netdev->ip = sockaddr.ipv4addr.addr;
			} else {
				return ENODEV;
			}
			break;
		}
		case SIOCGIFHWADDR: {
			ifreq_t *ifreq = arg;
			ifreq_t kifreq;
			int e = USERCOPY_POSSIBLY_FROM_USER(&kifreq, arg, sizeof(ifreq_t));
			if (e)
				return e;

			netdev_t *netdev = netdev_getdev(kifreq.name);
			if (netdev) {
				return USERCOPY_POSSIBLY_TO_USER(ifreq->addr.addr, netdev->mac.address, sizeof(mac_t));
			} else {
				return ENODEV;
			}
			break;
		}
		case SIOCADDRT: {
			abirtentry_t abirtentry;
			int e = USERCOPY_POSSIBLY_FROM_USER(&abirtentry, arg, sizeof(abirtentry_t));
			if (e)
				return e;

			sockaddr_t addr;
			sockaddr_t gateway;
			sockaddr_t mask;

			e = sock_convertaddress(&addr, &abirtentry.rt_dst);
			if (e)
				return e;

			e = sock_convertaddress(&gateway, &abirtentry.rt_gateway);
			if (e)
				return e;

			e = sock_convertaddress(&mask, &abirtentry.rt_genmask);
			if (e)
				return e;

			size_t devlen;
			e = USERCOPY_POSSIBLY_STRLEN_FROM_USER(abirtentry.rt_dev, &devlen);
			if (e)
				return e;

			char *dev = alloc(devlen + 1);
			if (dev == NULL)
				return ENOMEM;

			e = USERCOPY_POSSIBLY_FROM_USER(dev, abirtentry.rt_dev, devlen);
			if (e) {
				free(dev);
				return e;
			}

			netdev_t *netdev = netdev_getdev(dev);
			if (netdev == NULL) {
				free(dev);
				return ENOMEM;
			}

			e = auth_network_check(cred, AUTH_ACTIONS_NETWORK_CONFIGURE, NULL, netdev);
			if (e == 0)
				e = ipv4_addroute(netdev, addr.ipv4addr.addr, gateway.ipv4addr.addr, mask.ipv4addr.addr, abirtentry.rt_metric);

			free(dev);
			return e;
		}
			case FIONREAD: {
			socket_t *socket = SOCKFS_SOCKET_FROM_NODE(node);
			if (socket->ops->datacount == NULL)
				return ENOTTY;

			int count = socket->ops->datacount(socket);
			return USERCOPY_POSSIBLY_TO_USER(arg, &count, sizeof(int));
		}
			default:
				return ENOTTY;
	}
	return 0;
}

int sockfs_inactive(vnode_t *node) {
	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(node);
	INTERNAL_LOCK(node);
	socket->ops->destroy(socket);
	slab_free(nodecache, node);
	return 0;
}

// the mutex vnode is handled internally
// (as we can sleep when waiting for data/space, and the handling happens in the socket layer)
// this does mean that operations are not atomic between a INTERNAL_LOCK and INTERNAL_UNLOCK
static int sockfs_lock(vnode_t *) {
	return 0;
}

static int sockfs_unlock(vnode_t *) {
	return 0;
}

static int sockfs_enodev() {
	return ENODEV;
}

static vops_t vnops = {
	.create = sockfs_enodev,
	.open = sockfs_open,
	.close = sockfs_close,
	.getattr = sockfs_getattr,
	.setattr = sockfs_setattr,
	.lookup = sockfs_enodev,
	.poll = sockfs_poll,
	.read = sockfs_read,
	.write = sockfs_write,
	.access = sockfs_enodev,
	.unlink = sockfs_enodev,
	.link = sockfs_enodev,
	.symlink = sockfs_enodev,
	.readlink = sockfs_enodev,
	.inactive = sockfs_inactive,
	.mmap = sockfs_enodev,
	.munmap = sockfs_enodev,
	.getdents = sockfs_enodev,
	.resize = sockfs_enodev,
	.rename = sockfs_enodev,
	.ioctl = sockfs_ioctl,
	.putpage = sockfs_enodev,
	.getpage = sockfs_enodev,
	.sync = sockfs_enodev,
	.lock = sockfs_lock,
	.unlock = sockfs_unlock
};

static void ctor(scache_t *cache, void *obj) {
	socketnode_t *node = obj;
	memset(node, 0, sizeof(socketnode_t));
	VOP_INIT(&node->vnode, &vnops, 0, V_TYPE_SOCKET, NULL);
	node->attr.inode = ++currentinode;
}

void sockfs_init() {
	nodecache = slab_newcache(sizeof(socketnode_t), 0, ctor, ctor);
	__assert(nodecache);
}

int sockfs_newsocket(vnode_t **nodep, socket_t *socket) {
	vnode_t *vnode = slab_allocate(nodecache);
	socketnode_t *node = (socketnode_t *)vnode;
	if (node == NULL)
		return ENOMEM;

	node->socket = socket;
	*nodep = vnode;

	return 0;
}
