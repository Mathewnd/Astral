#include <kernel/vfs.h>
#include <kernel/devfs.h>
#include <hashtable.h>
#include <kernel/slab.h>
#include <logging.h>
#include <errno.h>
#include <kernel/alloc.h>
#include <kernel/timekeeper.h>
#include <kernel/pmm.h>
#include <kernel/vmmcache.h>
#include <kernel/auth.h>

static devnode_t *devfsroot;

static mutex_t tablelock;
static hashtable_t devtable;

static scache_t *nodecache;
static uintmax_t currentinode;

static vfsops_t vfsops;
static vops_t vnops;

#define INTERNAL_LOCK(v) MUTEX_ACQUIRE(&(v)->lock, false)
#define INTERNAL_UNLOCK(v) MUTEX_RELEASE(&(v)->lock);

static int devfs_mount(vfs_t **vfs, vnode_t *mountpoint, vnode_t *backing, void *data) {
	vfs_t *vfsp = alloc(sizeof(vfs_t));
	if (vfs == NULL)
		return ENOMEM;

	*vfs = vfsp;
	vfsp->ops = &vfsops;

	return 0;
}

static int devfs_root(vfs_t *vfs, vnode_t **node) {
	devfsroot->vnode.vfs = vfs;
	*node = (vnode_t *)devfsroot;
	return 0;
}

int devfs_open(vnode_t **node, int flags, cred_t *cred) {
	devnode_t *devnode = (devnode_t *)(*node);

	if ((*node)->type != V_TYPE_CHDEV && (*node)->type != V_TYPE_BLKDEV)
		return 0;

	if (devnode->master)
		devnode = devnode->master;

	if (devnode->devops->open == NULL)
		return 0;

	return devnode->devops->open(devnode->attr.rdevminor, node, flags);
}

int devfs_close(vnode_t *node, int flags, cred_t *cred) {
	devnode_t *devnode = (devnode_t *)node;

	if (node->type != V_TYPE_CHDEV && node->type != V_TYPE_BLKDEV)
		return 0;

	if (devnode->master)
		devnode = devnode->master;

	if (devnode->devops->close == NULL)
		return 0;

	return devnode->devops->close(devnode->attr.rdevminor, flags);
}

int devfs_read(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc, cred_t *cred) {
	if (node->type == V_TYPE_DIR)
		return EISDIR;

	if (node->type != V_TYPE_BLKDEV && node->type != V_TYPE_CHDEV)
		return ENODEV;

	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	if (devnode->devops->read == NULL)
		return ENODEV;

	// TODO remove this once the whole fs uses iovec
	iovec_iterator_t iovec_iterator;
	iovec_t iovec = {
		.addr = buffer,
		.len = size
	};

	iovec_iterator_init(&iovec_iterator, &iovec, 1);

	return devnode->devops->read(devnode->attr.rdevminor, &iovec_iterator, size, offset, flags, readc);
}

int devfs_write(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec, cred_t *cred) {
	if (node->type == V_TYPE_DIR)
		return EISDIR;

	if (node->type != V_TYPE_BLKDEV && node->type != V_TYPE_CHDEV)
		return ENODEV;

	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	if (devnode->devops->write == NULL)
		return ENODEV;

	// TODO remove this once the whole fs uses iovec
	iovec_iterator_t iovec_iterator;
	iovec_t iovec = {
		.addr = buffer,
		.len = size
	};

	iovec_iterator_init(&iovec_iterator, &iovec, 1);

	return devnode->devops->write(devnode->attr.rdevminor, &iovec_iterator, size, offset, flags, writec);
}

int devfs_lookup(vnode_t *node, char *name, vnode_t **result, cred_t *cred) {
	devnode_t *devnode = (devnode_t *)node;
	if (node->type != V_TYPE_DIR)
		return ENOTDIR;

	void *v;
	INTERNAL_LOCK(node);
	MUTEX_ACQUIRE(&tablelock, false);
	int error = hashtable_get(&devnode->children, &v, name, strlen(name));
	MUTEX_RELEASE(&tablelock);
	if (error) {
		INTERNAL_UNLOCK(node);
		return error;
	}

	vnode_t *rnode = v;
	VOP_HOLD(rnode);
	INTERNAL_UNLOCK(node);
	*result = rnode;

	return 0;
}

int devfs_getattr(vnode_t *node, vattr_t *attr, cred_t *cred) {
	devnode_t *devnode = (devnode_t *)node;

	if (devnode->physical && devnode->physical != node) {
		int err = VOP_GETATTR(devnode->physical, attr, cred);
		return err;
	}

	INTERNAL_LOCK(node);
	*attr = devnode->attr;
	attr->type = node->type;
	INTERNAL_UNLOCK(node);

	return 0;
}

int devfs_setattr(vnode_t *node, vattr_t *attr, int which, cred_t *cred) {
	devnode_t *devnode = (devnode_t *)node;

	if (devnode->physical && devnode->physical != node) {
		int err = VOP_SETATTR(devnode->physical, attr, which, cred);
		return err;
	}

	INTERNAL_LOCK(node);
	if (which & V_ATTR_GID)
		devnode->attr.gid = attr->gid;
	if (which & V_ATTR_UID)
		devnode->attr.uid = attr->uid;
	if (which & V_ATTR_MODE)
		devnode->attr.mode = attr->mode;
	if (which & V_ATTR_ATIME)
		devnode->attr.atime = attr->atime;
	if (which & V_ATTR_MTIME)
		devnode->attr.mtime = attr->mtime;
	if (which & V_ATTR_CTIME)
		devnode->attr.ctime = attr->ctime;
	INTERNAL_UNLOCK(node);
	return 0;
}

int devfs_poll(vnode_t *node, polldata_t *data, int events) {
	if (node->type != V_TYPE_BLKDEV && node->type != V_TYPE_CHDEV)
		return events;

	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	if (devnode->devops->poll == NULL)
		return events;

	return devnode->devops->poll(devnode->attr.rdevminor, data, events);
}

int devfs_mmap(vnode_t *node, void *addr, uintmax_t offset, int flags, cred_t *cred) {
	if (node->type != V_TYPE_BLKDEV && node->type != V_TYPE_CHDEV)
		return ENODEV;

	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	if (devnode->devops->mmap == NULL)
		return ENODEV;

	// if addr is this magic value, the caller is only
	// testing for mmap support from the device
	if (addr == VOP_MMAP_ADDRESS_MMAP_SUPPORTED)
		return 0;

	return devnode->devops->mmap(devnode->attr.rdevminor, addr, offset, flags);
}

int devfs_munmap(vnode_t *node, void *addr, uintmax_t offset, int flags, cred_t *cred) {
	if (node->type != V_TYPE_BLKDEV && node->type != V_TYPE_CHDEV)
		return ENODEV;

	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	if (devnode->devops->munmap == NULL)
		return ENODEV;

	return devnode->devops->munmap(devnode->attr.rdevminor, addr, offset, flags);
}

int devfs_access(vnode_t *vnode, mode_t mode, cred_t *cred) {
	return auth_filesystem_check(cred, auth_filesystem_convertaccess(mode), vnode, NULL) ? EACCES : 0;
}

int devfs_isatty(vnode_t *node) {
	if (node->type != V_TYPE_CHDEV)
		return ENOTTY;

	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	return devnode->devops->isatty ? devnode->devops->isatty(devnode->attr.rdevminor) : ENOTTY;
}

int devfs_ioctl(vnode_t *node, unsigned long request, void *arg, int *ret, cred_t *cred) {
	if (node->type != V_TYPE_BLKDEV && node->type != V_TYPE_CHDEV)
		return ENODEV;

	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	return devnode->devops->ioctl ? devnode->devops->ioctl(devnode->attr.rdevminor, request, arg, ret, cred) : ENOTTY;
}

int devfs_maxseek(vnode_t *node, size_t *max) {
	if (node->type != V_TYPE_BLKDEV && node->type != V_TYPE_CHDEV)
		return ENOTTY;

	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	return devnode->devops->maxseek ? devnode->devops->maxseek(devnode->attr.rdevminor, max) : ENOTTY;
}

int devfs_inactive(vnode_t *node) {
	INTERNAL_LOCK(node);
	devnode_t *devnode = (devnode_t *)node;
	vnode_t *master = (vnode_t *)devnode->master;
	if (devnode->physical && devnode->physical != node)
		VOP_RELEASE(devnode->physical);
	if (devnode->master)
		VOP_RELEASE(master);

	// only called when the master is inactive
	if (devnode->devops && devnode->devops->inactive)
		devnode->devops->inactive(devnode->attr.rdevminor);

	slab_free(nodecache, node);
	return 0;
}

int devfs_create(vnode_t *parent, char *name, vattr_t *attr, int type, vnode_t **result, cred_t *cred) {
	if (parent->type != V_TYPE_DIR)
		return ENOTDIR;

	if (type != V_TYPE_CHDEV && type != V_TYPE_BLKDEV && type != V_TYPE_DIR)
		return EINVAL;

	devnode_t *parentdevnode = (devnode_t *)parent;

	size_t namelen = strlen(name);
	void *v;
	INTERNAL_LOCK(parent);
	if (hashtable_get(&parentdevnode->children, &v, name, namelen) == 0) {
		INTERNAL_UNLOCK(parent);
		return EEXIST;
	}

	devnode_t *node = slab_allocate(nodecache);
	if (node == NULL) {
		INTERNAL_UNLOCK(parent);
		return ENOMEM;
	}

	timespec_t time = timekeeper_time();
	vattr_t tmpattr = *attr;
	tmpattr.atime = time;
	tmpattr.ctime = time;
	tmpattr.mtime = time;
	tmpattr.size = 0;
	__assert(devfs_setattr(&node->vnode, &tmpattr, V_ATTR_ALL, cred) == 0);
	node->attr.nlinks = 1;
	node->attr.rdevmajor = tmpattr.rdevmajor;
	node->attr.rdevminor = tmpattr.rdevminor;
	node->physical = &node->vnode;
	node->vnode.vfs = parent->vfs;
	node->vnode.type = type;

	if (type == V_TYPE_DIR) {
		int error = hashtable_init(&node->children, 100);
		if (error) {
			slab_free(nodecache, node);
			INTERNAL_UNLOCK(parent);
			return error;
		}
		if (hashtable_set(&node->children, node, ".", 1, true) || hashtable_set(&node->children, parent, "..", 2, true)) {
			hashtable_destroy(&node->children);
			slab_free(nodecache, node);
			INTERNAL_UNLOCK(parent);
			return error;
		}
	}

	int error = hashtable_set(&parentdevnode->children, node, name, namelen, true);
	if (error) {
		if (type == V_TYPE_DIR)
			hashtable_destroy(&node->children);
		INTERNAL_UNLOCK(parent);
		slab_free(nodecache, node);
		return error;
	}

	// held here to reduce cleanup
	if (type == V_TYPE_DIR)
		VOP_HOLD(parent);

	VOP_HOLD(&node->vnode);
	INTERNAL_UNLOCK(parent);
	*result = &node->vnode;

	return 0;
}

static int devfs_getdents(vnode_t *node, dent_t *buffer, size_t count, uintmax_t offset, size_t *readcount) {
	if (node->type != V_TYPE_DIR)
		return ENOTDIR;

	INTERNAL_LOCK(node);

	devnode_t *devnode = (devnode_t *)node;

	*readcount = 0;
	size_t current = 0;

	HASHTABLE_FOREACH(&devnode->children) {
		if (current < offset) {
			++current;
			continue;
		}

		if (*readcount == count)
			break;


		devnode_t *currnode = entry->value;
		dent_t *ent = &buffer[*readcount];

		ent->d_ino = currnode->attr.inode;
		ent->d_off = offset;
		ent->d_reclen = sizeof(dent_t);
		ent->d_type = vfs_getposixtype(currnode->vnode.type);
		strcpy(ent->d_name, entry->key);

		*readcount += 1;
	}

	INTERNAL_UNLOCK(node);
	return 0;
}

static int devfs_getpage(vnode_t *node, uintmax_t offset, struct page_t *page) {
	// only block devices will have this called
	__assert(node->type == V_TYPE_BLKDEV);
	size_t readc;
	int error = VOP_READ(node, MAKE_HHDM(pmm_getpageaddress(page)), PAGE_SIZE, offset, 0, &readc, NULL);
	if (readc == 0)
		return ENXIO;

	return error;
}

static int devfs_putpage(vnode_t *node, uintmax_t offset, struct page_t *page) {
	// only block devices will have this called
	__assert(node->type == V_TYPE_BLKDEV);
	size_t writec;
	int error = VOP_WRITE(node, MAKE_HHDM(pmm_getpageaddress(page)), PAGE_SIZE, offset, 0, &writec, NULL);
	__assert(writec != 0);

	return error;
}

static int devfs_sync(vnode_t *vnode) {
	return vmmcache_sync(vnode);
}

// most locking is handled by the devices.
// we only care about the tree side of things, and each function
// will have their own handling of the vnode mutex with the
// INTERNAL_LOCK and INTERNAL_UNLOCK macros.
//
// the current handling does mean that it is not atomic between operations.
// a future locking implementation could lock only specific types, such as directories.
static int devfs_lock(vnode_t *vnode) {
	return 0;
}

static int devfs_unlock(vnode_t *vnode) {
	return 0;
}

static int devfs_enodev() {
	return ENODEV;
}

static vfsops_t vfsops = {
	.mount = devfs_mount,
	.root = devfs_root
};

static vops_t vnops = {
	.create = devfs_create,
	.open = devfs_open,
	.close = devfs_close,
	.getattr = devfs_getattr,
	.setattr = devfs_setattr,
	.lookup = devfs_lookup,
	.poll = devfs_poll,
	.read = devfs_read,
	.write = devfs_write,
	.access = devfs_access,
	.unlink = devfs_enodev,
	.link = devfs_enodev,
	.symlink = devfs_enodev,
	.readlink = devfs_enodev,
	.inactive = devfs_inactive,
	.mmap = devfs_mmap,
	.munmap = devfs_munmap,
	.getdents = devfs_getdents,
	.isatty = devfs_isatty, 
	.ioctl = devfs_ioctl,
	.maxseek = devfs_maxseek,
	.resize = devfs_enodev,
	.rename = devfs_enodev,
	.putpage = devfs_putpage,
	.getpage = devfs_getpage,
	.sync = devfs_sync,
	.lock = devfs_lock,
	.unlock = devfs_unlock
};

static void ctor(scache_t *cache, void *obj) {
	devnode_t *node = obj;
	memset(node, 0, sizeof(devnode_t));
	VOP_INIT(&node->vnode, &vnops, 0, 0, NULL)
	node->attr.inode = ++currentinode;
}

void devfs_init() {
	__assert(hashtable_init(&devtable, 50) == 0);
	nodecache = slab_newcache(sizeof(devnode_t), 0, ctor, ctor);
	MUTEX_INIT(&tablelock);
	__assert(nodecache);

	devfsroot = slab_allocate(nodecache);
	__assert(devfsroot);
	devfsroot->vnode.type = V_TYPE_DIR;
	devfsroot->vnode.flags = V_FLAGS_ROOT;
	devfsroot->attr.mode = 0755;
	__assert(hashtable_init(&devfsroot->children, 100) == 0);
	__assert(vfs_register(&vfsops, "devfs") == 0);
	__assert(hashtable_set(&devfsroot->children, devfsroot, ".", 1, true) == 0);
}

int devfs_getbyname(char *name, vnode_t **ret) {
	int error = vfs_lookup(ret, (vnode_t *)devfsroot, name, NULL, 0);
	if (error)
		goto cleanup;

	VOP_UNLOCK(*ret);
	cleanup:
	return error;
}
// register new device 
int devfs_register(devops_t *devops, char *name, int type, int major, int minor, mode_t mode, cred_t *cred) {
	__assert(type == V_TYPE_CHDEV || type == V_TYPE_BLKDEV);
	devnode_t *master = slab_allocate(nodecache);
	if (master == NULL)
		return ENOMEM;

	master->devops = devops;

	int key[2] = {major, minor};

	MUTEX_ACQUIRE(&tablelock, false);
	int error = hashtable_set(&devtable, master, key, sizeof(key), true);
	MUTEX_RELEASE(&tablelock);
	if (error) {
		slab_free(nodecache, master);
		return error;
	}

	vattr_t attr = {0};
	attr.mode = mode;
	attr.rdevmajor = major;
	attr.rdevminor = minor;
	attr.uid = cred == NULL ? 0 : cred->euid;
	attr.gid = cred == NULL ? 0 : cred->egid;

	vnode_t *dir = NULL;
	char lastcomp[strlen(name) + 1];
	error = vfs_lookup(&dir, (vnode_t *)devfsroot, name, lastcomp, VFS_LOOKUP_PARENT);
	if (error) {
		slab_free(nodecache, master);
		return error;
	}

	vnode_t *newvnode = NULL;
	// the devfs node will have a reference from both the devtable and from the fs link
	error = VOP_CREATE(dir, lastcomp, &attr, type, &newvnode, NULL);

	// locked by vfs_lookup
	VOP_UNLOCK(dir);
	VOP_RELEASE(dir);

	if (error) {
		MUTEX_ACQUIRE(&tablelock, false);
		__assert(hashtable_remove(&devtable, key, sizeof(key) == 0));
		MUTEX_RELEASE(&tablelock);
		slab_free(nodecache, master);
		return error;
	}

	devnode_t *newdevnode = (devnode_t *)newvnode;
	master->attr = newdevnode->attr;
	master->vnode.type = type;
	newdevnode->master = master;
	// locked by VOP_CREATE
	VOP_UNLOCK(newvnode);

	return 0;
}

// allocate a pointer node to the master node associated with device major and minor
int devfs_getnode(vnode_t *physical, int major, int minor, vnode_t **node) {
	int key[2] = {major, minor};
	MUTEX_ACQUIRE(&tablelock, false);
	void *r;
	int err = hashtable_get(&devtable, &r, key, sizeof(key));
	MUTEX_RELEASE(&tablelock);
	if (err)
		return err == ENOENT ? ENXIO : err;

	devnode_t *newnode = slab_allocate(nodecache);
	if (newnode == NULL)
		return ENOMEM;

	devnode_t *master = r;
	newnode->master = master;
	VOP_HOLD(&master->vnode);
	if (physical) {
		newnode->physical = physical;
		VOP_HOLD(physical);
	}
	*node = &newnode->vnode;
	newnode->vnode.type = master->vnode.type;
	return 0;
}

void devfs_remove(char *name, int major, int minor) {
	int key[2] = {major, minor};
	// remove devtable reference
	MUTEX_ACQUIRE(&tablelock, false);
	__assert(hashtable_remove(&devtable, key, sizeof(key)) == 0);
	MUTEX_RELEASE(&tablelock);

	// get parent
	vnode_t *parent;
	char namebuff[100];
	__assert(vfs_lookup(&parent, (vnode_t *)devfsroot, name, namebuff, VFS_LOOKUP_PARENT) == 0);
	devnode_t *parentdevnode = (devnode_t *)parent;

	INTERNAL_LOCK(parent);

	// get device vnode
	void *tmp;
	__assert(hashtable_get(&parentdevnode->children, &tmp, namebuff, strlen(namebuff)) == 0);
	vnode_t *vnode = tmp;
	devnode_t *node = tmp;
	__assert(node->attr.rdevmajor == major && node->attr.rdevminor == minor);

	__assert(hashtable_remove(&parentdevnode->children, namebuff, strlen(namebuff)) == 0);

	INTERNAL_UNLOCK(parent);

	// release all the references to the removed node:
	VOP_RELEASE(vnode);
	VOP_RELEASE(vnode);
}

int devfs_createdir(char *name) {
	vattr_t attr = {0};
	return vfs_create((vnode_t *)devfsroot, name, &attr, V_TYPE_DIR, NULL);
}
