#include <kernel/tmpfs.h>
#include <logging.h>
#include <kernel/slab.h>
#include <errno.h>
#include <kernel/alloc.h>
#include <kernel/timekeeper.h>
#include <kernel/vmm.h>
#include <util.h>
#include <kernel/abi.h>

static scache_t *nodecache;
static tmpfsnode_t *newnode(vfs_t *vfs, int type);
static void freenode(tmpfsnode_t *node);
static vfsops_t vfsops; 

static int tmpfs_mount(vfs_t **vfs, vnode_t *mountpoint, vnode_t *backing, void *data) {
	tmpfs_t *tmpfs = alloc(sizeof(tmpfs_t));
	if (tmpfs == NULL)
		return ENOMEM;

	*vfs = (vfs_t *)tmpfs;
	tmpfs->vfs.ops = &vfsops;

	return 0;
}

static int tmpfs_poll(vnode_t *node, int event) {
	int ret = 0;

	if (event & POLLIN)
		ret |= POLLIN;
	if (event & POLLOUT)
		ret |= POLLOUT;

	return ret;
}

#define DATAMMUFLAGS (ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_NOEXEC)

static int tmpfs_resize(tmpfsnode_t *node, size_t size) {
	if (size == node->attr.size || node->vnode.type != V_TYPE_REGULAR)
		return 0;

	// if smaller, don't deallocate any pages
	if (size < node->attr.size) {
		node->attr.size = size;
		return 0;
	}

	// no mapping yet
	if (node->pagecount == 0) {
		size_t mapsize = ROUND_UP(size, PAGE_SIZE);
		node->data = vmm_map(NULL, mapsize, VMM_FLAGS_ALLOCATE, DATAMMUFLAGS, NULL);
		if (node->data == NULL)
			return ENOMEM;
		memset(node->data, 0, size);
		node->pagecount = mapsize / PAGE_SIZE;
		node->attr.size = size;
		return 0;
	}
	
	size_t diff = size - node->attr.size;
	// no need for new pages
	if (size < node->pagecount * PAGE_SIZE) {
		memset((void *)((uintptr_t)node->data + node->attr.size), 0, diff);
		node->attr.size = size;
		return 0;
	}

	// double the capacity or, if new size larger than double, resize to fit
	size_t newcount = size > node->pagecount * 2 * PAGE_SIZE ? ROUND_UP(size, PAGE_SIZE) / PAGE_SIZE : node->pagecount * 2;

	void *newdata = vmm_map(NULL, newcount, VMM_FLAGS_PAGESIZE | VMM_FLAGS_ALLOCATE, DATAMMUFLAGS, NULL);
	if (newdata == NULL)
		return ENOMEM;

	memcpy(newdata, node->data, node->attr.size);
	memset(((void *)(uintptr_t)newdata + node->attr.size), 0, diff);
	vmm_unmap(node->data, node->pagecount, VMM_FLAGS_PAGESIZE);

	node->pagecount = newcount;
	node->data = newdata;
	node->attr.size = size;

	return 0;
}

static int tmpfs_getattr(vnode_t *node, vattr_t *attr, cred_t *cred) {
	tmpfsnode_t *tmpnode = (tmpfsnode_t *)node;
	*attr = tmpnode->attr;
	return 0;
}

static int tmpfs_setattr(vnode_t *node, vattr_t *attr, cred_t *cred) {
	tmpfsnode_t *tmpnode = (tmpfsnode_t *)node;
	int err = tmpfs_resize(tmpnode, attr->size);
	if (err)
		return err;
	tmpnode->attr.gid = attr->gid;
	tmpnode->attr.uid = attr->uid;
	tmpnode->attr.mode = attr->mode;
	tmpnode->attr.atime = attr->atime;
	tmpnode->attr.mtime = attr->mtime;
	tmpnode->attr.ctime = attr->ctime;
	return 0;
}

static int tmpfs_lookup(vnode_t *parent, char *name, vnode_t **result, cred_t *cred) {
	tmpfsnode_t *tmpparent = (tmpfsnode_t *)parent;
	vnode_t *child = NULL;
	if (parent->type != V_TYPE_DIR)
		return ENOTDIR;

	void *r;
	int err = hashtable_get(&tmpparent->children, &r, name, strlen(name));
	if (err)
		return err;

	child = r;
	VOP_HOLD(child);
	*result = child;
	return 0;
}

static int tmpfs_create(vnode_t *parent, char *name, vattr_t *attr, int type, vnode_t **result, cred_t *cred) {
	if (parent->type != V_TYPE_DIR)
		return ENOTDIR;

	tmpfsnode_t *parenttmpnode = (tmpfsnode_t *)parent;
	size_t namelen = strlen(name);
	void *v;

	if (hashtable_get(&parenttmpnode->children, &v, name, namelen) == 0)
		return EEXIST;

	tmpfsnode_t *tmpnode = newnode(parent->vfs, type);
	vnode_t *node = (vnode_t *)tmpnode;
	if (tmpnode == NULL)
		return ENOMEM;

	timespec_t time = timekeeper_time();

	vattr_t tmpattr = *attr;
	tmpattr.atime = time;
	tmpattr.ctime = time;
	tmpattr.mtime = time;
	tmpattr.size = 0;
	int error = tmpfs_setattr(node, &tmpattr, cred);
	tmpnode->attr.nlinks = 1;

	if (error) {
		VOP_RELEASE(node);
		return error; 
	}

	// if a dir, create the .. entry (the . entry is already made in newnode() if it has the dir type)
	if (type == V_TYPE_DIR) {
		error = hashtable_set(&tmpnode->children, parenttmpnode, "..", 2, true);
		if (error) {
			VOP_RELEASE(node);
			return error;
		}
	}

	error = hashtable_set(&parenttmpnode->children, tmpnode, name, namelen, true);

	if (error) {
		VOP_RELEASE(node);
		return error;
	}

	// hold the .. reference (done here to reduce cleanup)
	if (type == V_TYPE_DIR)
		VOP_HOLD(parent);

	node->vfs = parent->vfs;
	*result = node;
	VOP_HOLD(node);

	return error;
}

static int tmpfs_open(vnode_t **node, int flag, cred_t *cred) {
	return 0;
}

static int tmpfs_close(vnode_t *node, int flag, cred_t *cred) {
	return 0;
}

static int tmpfs_root(vfs_t *vfs, vnode_t **vnode) {
	if (vfs->root) {
		*vnode = vfs->root;
		return 0;
	}

	tmpfsnode_t *node = newnode(vfs, V_TYPE_DIR);
	if (node == NULL)
		return ENOMEM;

	*vnode = (vnode_t *)node;
	vfs->root = *vnode;

	node->vnode.flags |= V_FLAGS_ROOT;

	return 0;
}

static int tmpfs_read(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc, cred_t *cred) {
	tmpfsnode_t *tmpnode = (tmpfsnode_t *)node;
	if (node->type != V_TYPE_REGULAR)
		return EINVAL;

	// offset past end of file
	if (offset >= tmpnode->attr.size) {
		*readc = 0;
		return 0;
	}

	// rtop of read goes past end of file
	if (offset + size > tmpnode->attr.size)
		size = tmpnode->attr.size - offset;

	*readc = size;
	memcpy(buffer, (void *)((uintptr_t)tmpnode->data + offset), size);

	return 0;
}

static int tmpfs_write(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec, cred_t *cred) {
	tmpfsnode_t *tmpnode = (tmpfsnode_t *)node;
	if (node->type != V_TYPE_REGULAR)
		return EINVAL;

	size_t top = offset + size;

	if (top > tmpnode->attr.size) {
		int err = tmpfs_resize(tmpnode, top);
		if (err)
			return err;
	}

	*writec = size;
	memcpy((void *)((uintptr_t)tmpnode->data + offset), buffer, size);

	return 0;
}

static int tmpfs_access(vnode_t *node, mode_t mode, cred_t *cred) {
	// TODO permission checks
	return 0;
}

static int tmpfs_unlink(vnode_t *node, char *name, cred_t *cred) {
	tmpfsnode_t *tmpnode = (tmpfsnode_t *)node;
	if (node->type != V_TYPE_DIR)
		return ENOTDIR;

	size_t namelen = strlen(name);
	void *r;
	int err = hashtable_get(&tmpnode->children, &r, name, namelen);
	if (err)
		return err;

	vnode_t *unlinknode = r;
	tmpfsnode_t *unlinktmpnode = r;

	err = hashtable_remove(&tmpnode->children, name, namelen);
	if (err)
		return err;

	VOP_LOCK(unlinknode);
	--unlinktmpnode->attr.nlinks;
	VOP_UNLOCK(unlinknode);
	VOP_RELEASE(unlinknode);

	return 0;
}

static int tmpfs_link(vnode_t *node, vnode_t *dir, char *name, cred_t *cred) {
	if (dir->type != V_TYPE_DIR)
		return ENOTDIR;

	tmpfsnode_t *tmpdir = (tmpfsnode_t *)dir;
	size_t namelen = strlen(name);
	void *v;
	if (hashtable_get(&tmpdir->children, &v, name, namelen) == 0)
		return EEXIST;

	int error = hashtable_set(&tmpdir->children, node, name, namelen, true);
	if (error)
		return error;

	VOP_HOLD(node);
	tmpfsnode_t *tmpnode = (tmpfsnode_t *)node;
	++tmpnode->attr.nlinks;

	return 0;
}

static int tmpfs_symlink(vnode_t *node, char *name, vattr_t *attr, char *path, cred_t *cred) {
	if (node->type != V_TYPE_DIR)
		return ENOTDIR;

	char *pathbuf = alloc(strlen(path) + 1);
	if (pathbuf == NULL)
		return ENOMEM;

	vnode_t *linknode = NULL;

	int err = tmpfs_create(node, name, attr, V_TYPE_LINK, &linknode, cred);
	if (err) {
		free(pathbuf);
		return err;
	}

	tmpfsnode_t *linktmpnode = (tmpfsnode_t *)linknode;
	linktmpnode->link = pathbuf;
	VOP_RELEASE(linknode);

	return 0;
}

static int tmpfs_readlink(vnode_t *node, char **link, cred_t *cred) {
	if (node->type != V_TYPE_LINK)
		return EINVAL;

	tmpfsnode_t *tmpnode = (tmpfsnode_t *)node;

	char *ret = alloc(strlen(tmpnode->link) + 1);
	if (ret == NULL)
		return ENOMEM;

	strcpy(ret, tmpnode->link);
	*link = ret;

	return 0;
}

static int tmpfs_inactive(vnode_t *node) {
	freenode((tmpfsnode_t *)node);
	return 0;
}

static vfsops_t vfsops = {
	.mount = tmpfs_mount,
	.root = tmpfs_root
};

static vops_t vnops = {
	.create = tmpfs_create,
	.open = tmpfs_open,
	.close = tmpfs_close,
	.getattr = tmpfs_getattr,
	.setattr = tmpfs_setattr,
	.lookup = tmpfs_lookup,
	.poll = tmpfs_poll,
	.read = tmpfs_read,
	.write = tmpfs_write,
	.access = tmpfs_access,
	.unlink = tmpfs_unlink,
	.link = tmpfs_link,
	.symlink = tmpfs_symlink,
	.readlink = tmpfs_readlink,
	.inactive = tmpfs_inactive
};

static tmpfsnode_t *newnode(vfs_t *vfs, int type) {
	tmpfsnode_t *node = slab_allocate(nodecache);
	if (node == NULL)
		return NULL;

	if (type == V_TYPE_DIR) {
		if (hashtable_init(&node->children, 32)) {
			slab_free(nodecache, node);
			return NULL;
		}

		if (hashtable_set(&node->children, node, ".", 1, true)) {
			hashtable_destroy(&node->children);
			slab_free(nodecache, node);
			return NULL;
		}
	}

	memset(&node->attr, 0, sizeof(vattr_t));
	node->attr.type = type;
	node->attr.inode = ++((tmpfs_t *)vfs)->inodenumber;
	node->vnode.type = type;
	node->vnode.refcount = 1;
	SPINLOCK_INIT(node->vnode.lock);
	node->vnode.ops = &vnops;
	node->vnode.vfs = vfs;
	return node;
}

static void freenode(tmpfsnode_t *node) {
	__assert(node->vnode.refcount == 0);
	switch (node->vnode.type) {
		case V_TYPE_LINK:
			if (node->link)
				free(node->link);
			break;
		case V_TYPE_DIR:
			__assert(node->children.entrycount == 2); // assert that it only has the dot entries
			// remove refcount of the .. dir
			void *r = NULL;
			__assert(hashtable_get(&node->children, &r, "..", 2) == 0);
			vnode_t *pnode = r;
			VOP_RELEASE(pnode);
			__assert(hashtable_destroy(&node->children) == 0);
			break;
		case V_TYPE_REGULAR:
			vmm_unmap(node->data, node->pagecount, VMM_FLAGS_PAGESIZE);
		default:
			break;
	}
	slab_free(nodecache, node);
}

void tmpfs_init() {
	__assert(vfs_register(&vfsops, "tmpfs") == 0);
	nodecache = slab_newcache(sizeof(tmpfsnode_t), 0, NULL, NULL);
	__assert(nodecache);
}
