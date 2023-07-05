#include <kernel/vfs.h>
#include <kernel/devfs.h>
#include <hashtable.h>
#include <kernel/slab.h>
#include <logging.h>
#include <errno.h>
#include <kernel/alloc.h>
#include <kernel/timekeeper.h>

static devnode_t *devfsroot;

static spinlock_t tablelock;
static hashtable_t devtable;
static hashtable_t nametable;

static scache_t *nodecache;
static uintmax_t currentinode;

static vfsops_t vfsops;
static vops_t vnops;

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

	if (devnode->devops == NULL)
		return 0;

	if (devnode->master)
		devnode = devnode->master;

	if (devnode->devops->open == NULL)
		return 0;

	return devnode->devops->open(devnode->attr.rdevminor, flags);
}

int devfs_close(vnode_t *node, int flags, cred_t *cred) {
	devnode_t *devnode = (devnode_t *)node;

	if (devnode->devops == NULL)
		return 0;

	if (devnode->master)
		devnode = devnode->master;

	if (devnode->devops->close == NULL)
		return 0;

	return devnode->devops->close(devnode->attr.rdevminor, flags);
}

int devfs_read(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc, cred_t *cred) {
	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	if (devnode->devops->read == NULL)
		return ENODEV;

	return devnode->devops->read(devnode->attr.rdevminor, buffer, size, offset, flags, readc);
}

int devfs_write(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec, cred_t *cred) {
	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	if (devnode->devops->write == NULL)
		return ENODEV;

	return devnode->devops->write(devnode->attr.rdevminor, buffer, size, offset, flags, writec);
}

int devfs_lookup(vnode_t *node, char *name, vnode_t **result, cred_t *cred) {
	if (node != (vnode_t *)devfsroot)
		return EINVAL;

	void *v;
	VOP_LOCK(node);
	int error = hashtable_get(&nametable, &v, name, strlen(name));
	if (error) {
		VOP_UNLOCK(node);
		return error;
	}

	vnode_t *rnode = v;
	VOP_HOLD(rnode);
	VOP_UNLOCK(node);
	*result = rnode;

	return 0;
}

int devfs_getattr(vnode_t *node, vattr_t *attr, cred_t *cred) {
	devnode_t *devnode = (devnode_t *)node;

	if (devnode->physical && devnode->physical != node) {
		VOP_LOCK(devnode->physical);
		int err = VOP_SETATTR(devnode->physical, attr, cred);
		VOP_UNLOCK(devnode->physical);
		return err;
	}

	VOP_LOCK(node);
	*attr = devnode->attr;
	attr->type = node->type;
	VOP_UNLOCK(node);

	return 0;
}

int devfs_setattr(vnode_t *node, vattr_t *attr, cred_t *cred) {
	devnode_t *devnode = (devnode_t *)node;

	if (devnode->physical && devnode->physical != node) {
		int err = VOP_GETATTR(devnode->physical, attr, cred);
		return err;
	}

	VOP_LOCK(node);
	devnode->attr.gid = attr->gid;
	devnode->attr.uid = attr->uid;
	devnode->attr.mode = attr->mode;
	devnode->attr.atime = attr->atime;
	devnode->attr.mtime = attr->mtime;
	devnode->attr.ctime = attr->ctime;
	VOP_UNLOCK(node);
	return 0;
}

int devfs_poll(vnode_t *node, int events) {
	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	if (devnode->devops->poll == NULL)
		return ENODEV;

	return devnode->devops->poll(devnode->attr.rdevminor, events);
}

int devfs_mmap(vnode_t *node, void *addr, uintmax_t offset, int flags, cred_t *cred) {
	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	if (devnode->devops->mmap == NULL)
		return ENODEV;

	return devnode->devops->mmap(devnode->attr.rdevminor, addr, offset, flags);
}

int devfs_munmap(vnode_t *node, void *addr, uintmax_t offset, int flags, cred_t *cred) {
	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	if (devnode->devops->munmap == NULL)
		return ENODEV;

	return devnode->devops->munmap(devnode->attr.rdevminor, addr, offset, flags);
}

int devfs_access(vnode_t *node, mode_t mode, cred_t *cred) {
	// TODO permission checks
	return 0;
}

int devfs_isatty(vnode_t *node) {
	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	return devnode->devops->isatty ? devnode->devops->isatty(devnode->attr.rdevminor) : ENOTTY;
}

int devfs_ioctl(vnode_t *node, unsigned long request, void *arg, int *ret) {
	devnode_t *devnode = (devnode_t *)node;
	if (devnode->master)
		devnode = devnode->master;

	return devnode->devops->ioctl ? devnode->devops->ioctl(devnode->attr.rdevminor, request, arg, ret) : ENOTTY;
}

int devfs_inactive(vnode_t *node) {
	VOP_LOCK(node);
	devnode_t *devnode = (devnode_t *)node;
	vnode_t *master = (vnode_t *)devnode->master;
	if (devnode->physical && devnode->physical != node)
		VOP_RELEASE(devnode->physical);
	if (devnode->master)
		VOP_RELEASE(master);
	slab_free(nodecache, node);
	return 0;
}

int devfs_create(vnode_t *parent, char *name, vattr_t *attr, int type, vnode_t **result, cred_t *cred) {
	if (parent != (vnode_t *)devfsroot || (type != V_TYPE_CHDEV && type != V_TYPE_BLKDEV))
		return EINVAL;

	size_t namelen = strlen(name);
	void *v;
	VOP_LOCK(parent);
	if (hashtable_get(&nametable, &v, name, namelen) == 0) {
		VOP_UNLOCK(parent);
		return EEXIST;
	}

	devnode_t *node = slab_allocate(nodecache);
	if (node == NULL) {
		VOP_UNLOCK(parent);
		return ENOMEM;
	}

	timespec_t time = timekeeper_time();
	vattr_t tmpattr = *attr;
	tmpattr.atime = time;
	tmpattr.ctime = time;
	tmpattr.mtime = time;
	tmpattr.size = 0;
	__assert(devfs_setattr(&node->vnode, &tmpattr, cred) == 0);
	node->attr.nlinks = 1;
	node->attr.rdevmajor = tmpattr.rdevmajor;
	node->attr.rdevminor = tmpattr.rdevminor;
	node->physical = &node->vnode;
	node->vnode.vfs = parent->vfs;
	node->vnode.type = type;

	int error = hashtable_set(&nametable, node, name, namelen, true);
	if (error) {
		VOP_UNLOCK(parent);
		slab_free(nodecache, node);
		return error;
	}

	VOP_HOLD(&node->vnode);
	VOP_UNLOCK(parent);
	*result = &node->vnode;

	return 0;
}

static int devfs_getdents(vnode_t *node, dent_t *buffer, size_t count, uintmax_t offset, size_t *readcount) {
	if (node != (vnode_t *)devfsroot)
		return EINVAL;

	VOP_LOCK(node);

	devnode_t *devnode = (devnode_t *)node;

	*readcount = 0;
	size_t current = 0;

	HASHTABLE_FOREACH(&nametable) {
		if (current < offset) {
			++current;
			continue;
		}

		if (*readcount == count)
			break;

		dent_t *ent = &buffer[*readcount];

		ent->d_ino = devnode->attr.inode;
		ent->d_off = offset;
		ent->d_reclen = sizeof(dent_t);
		ent->d_type = vfs_getposixtype(node->type);
		strcpy(ent->d_name, entry->key);

		*readcount += 1;
	}

	VOP_UNLOCK(node);
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
	.inactive = devfs_enodev,
	.mmap = devfs_mmap,
	.munmap = devfs_munmap,
	.getdents = devfs_getdents,
	.isatty = devfs_isatty,
	.ioctl = devfs_ioctl
};

static void ctor(scache_t *cache, void *obj) {
	devnode_t *node = obj;
	memset(node, 0, sizeof(devnode_t));
	node->vnode.refcount = 1;
	SPINLOCK_INIT(node->vnode.lock);
	node->vnode.ops = &vnops;
	node->attr.inode = ++currentinode;
}

void devfs_init() {
	__assert(hashtable_init(&devtable, 50) == 0);
	__assert(hashtable_init(&nametable, 50) == 0);
	nodecache = slab_newcache(sizeof(devnode_t), 0, ctor, ctor);
	SPINLOCK_INIT(tablelock);
	__assert(nodecache);

	devfsroot = slab_allocate(nodecache);
	__assert(devfsroot);
	devfsroot->vnode.type = V_TYPE_DIR;
	devfsroot->vnode.flags = V_FLAGS_ROOT;
	__assert(vfs_register(&vfsops, "devfs") == 0);
	__assert(hashtable_set(&nametable, devfsroot, ".", 1, true) == 0);
}

// register new device 
int devfs_register(devops_t *devops, char *name, int type, int major, int minor, mode_t mode) {
	__assert(type == V_TYPE_CHDEV || type == V_TYPE_BLKDEV);
	devnode_t *master = slab_allocate(nodecache);
	if (master == NULL)
		return ENOMEM;

	master->devops = devops;

	int key[2] = {major, minor};

	spinlock_acquire(&tablelock);
	int error = hashtable_set(&devtable, master, key, sizeof(key), true);
	spinlock_release(&tablelock);
	if (error) {
		slab_free(nodecache, master);
		return error;
	}

	vattr_t attr = {0};
	attr.mode = mode;
	attr.rdevmajor = major;
	attr.rdevminor = minor;

	vnode_t *newvnode = NULL;

	error = vfs_create((vnode_t *)devfsroot, name, &attr, type, &newvnode);
	if (error) {
		spinlock_acquire(&tablelock);
		__assert(hashtable_remove(&devtable, key, sizeof(key) == 0));
		spinlock_release(&tablelock);
		slab_free(nodecache, master);
		return error;
	}

	devnode_t *newdevnode = (devnode_t *)newvnode;
	master->attr = newdevnode->attr;
	newdevnode->master = master;
	VOP_RELEASE(newvnode);

	return 0;
}

// allocate a pointer node to the master node associated with device major and minor
int devfs_getnode(vnode_t *physical, int major, int minor, vnode_t **node) {
	int key[2] = {major, minor};
	spinlock_acquire(&tablelock);
	void *r;
	int err = hashtable_get(&devtable, &r, key, sizeof(key));
	spinlock_release(&tablelock);
	if (err)
		return err == ENOENT ? ENXIO : err;

	devnode_t *newnode = slab_allocate(nodecache);
	if (newnode)
		return ENOMEM;

	devnode_t *master = r;
	newnode->master = master;
	VOP_HOLD(&master->vnode);
	newnode->physical = physical;
	VOP_HOLD(physical);
	*node = &newnode->vnode;
	return 0;
}
