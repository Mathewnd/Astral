#include <kernel/tmpfs.h>
#include <logging.h>
#include <kernel/slab.h>
#include <errno.h>
#include <kernel/alloc.h>
#include <kernel/timekeeper.h>
#include <kernel/vmm.h>
#include <util.h>
#include <kernel/abi.h>
#include <kernel/devfs.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <arch/cpu.h>
#include <string.h>

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

#define DATAMMUFLAGS (ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_NOEXEC)

// expects to be called with node locked
static int resize(tmpfsnode_t *node, size_t size) {
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
	VOP_LOCK(node);
	tmpfsnode_t *tmpnode = (tmpfsnode_t *)node;
	*attr = tmpnode->attr;
	attr->blocksused = node->type == V_TYPE_DIR ? 1 : tmpnode->pagecount;
	VOP_UNLOCK(node);
	return 0;
}

static int tmpfs_setattr(vnode_t *node, vattr_t *attr, cred_t *cred) {
	VOP_LOCK(node);
	tmpfsnode_t *tmpnode = (tmpfsnode_t *)node;

	tmpnode->attr.gid = attr->gid;
	tmpnode->attr.uid = attr->uid;
	tmpnode->attr.mode = attr->mode;
	tmpnode->attr.atime = attr->atime;
	tmpnode->attr.mtime = attr->mtime;
	tmpnode->attr.ctime = attr->ctime;
	VOP_UNLOCK(node);
	return 0;
}

static int tmpfs_lookup(vnode_t *parent, char *name, vnode_t **result, cred_t *cred) {
	tmpfsnode_t *tmpparent = (tmpfsnode_t *)parent;
	vnode_t *child = NULL;
	if (parent->type != V_TYPE_DIR)
		return ENOTDIR;

	VOP_LOCK(parent);
	void *r;
	int err = hashtable_get(&tmpparent->children, &r, name, strlen(name));
	if (err) {
		VOP_UNLOCK(parent);
		return err;
	}

	child = r;
	VOP_HOLD(child);
	VOP_UNLOCK(parent);
	*result = child;
	return 0;
}

static int tmpfs_create(vnode_t *parent, char *name, vattr_t *attr, int type, vnode_t **result, cred_t *cred) {
	if (parent->type != V_TYPE_DIR)
		return ENOTDIR;

	tmpfsnode_t *parenttmpnode = (tmpfsnode_t *)parent;
	size_t namelen = strlen(name);
	void *v;

	VOP_LOCK(parent);
	if (hashtable_get(&parenttmpnode->children, &v, name, namelen) == 0) {
		VOP_UNLOCK(parent);
		return EEXIST;
	}

	tmpfsnode_t *tmpnode = newnode(parent->vfs, type);
	vnode_t *node = (vnode_t *)tmpnode;
	if (tmpnode == NULL) {
		VOP_UNLOCK(parent);
		return ENOMEM;
	}

	timespec_t time = timekeeper_time();

	vattr_t tmpattr = *attr;
	tmpattr.atime = time;
	tmpattr.ctime = time;
	tmpattr.mtime = time;
	tmpattr.size = 0;
	tmpattr.type = type;
	int error = tmpfs_setattr(node, &tmpattr, cred);
	tmpnode->attr.nlinks = 1;

	if (error) {
		VOP_UNLOCK(parent);
		VOP_RELEASE(node);
		return error; 
	}

	// if a dir, create the .. entry (the . entry is already made in newnode() if it has the dir type)
	if (type == V_TYPE_DIR) {
		error = hashtable_set(&tmpnode->children, parenttmpnode, "..", 2, true);
		if (error) {
			VOP_UNLOCK(parent);
			VOP_RELEASE(node);
			return error;
		}
	}

	error = hashtable_set(&parenttmpnode->children, tmpnode, name, namelen, true);
	VOP_UNLOCK(parent);

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

static int tmpfs_open(vnode_t **nodep, int flag, cred_t *cred) {
	vnode_t *node = *nodep;
	tmpfsnode_t *tmpnode = (tmpfsnode_t *)node;

	if (node->type == V_TYPE_CHDEV || node->type == V_TYPE_BLKDEV) {
		vnode_t *devnode;
		int err = devfs_getnode(node, tmpnode->attr.rdevmajor, tmpnode->attr.rdevminor, &devnode);
		if (err)
			return err;

		VOP_RELEASE(node);
		VOP_HOLD(devnode);
		*nodep = devnode; 
	}

	return 0;
}

static int tmpfs_close(vnode_t *node, int flag, cred_t *cred) {
	return 0;
}

static int tmpfs_root(vfs_t *vfs, vnode_t **vnode) {
	// FIXME lock vfs
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

	VOP_LOCK(node);
	// offset past end of file
	if (offset >= tmpnode->attr.size) {
		*readc = 0;
		VOP_UNLOCK(node);
		return 0;
	}

	// top of read goes past end of file
	if (offset + size > tmpnode->attr.size)
		size = tmpnode->attr.size - offset;

	*readc = size;
	memcpy(buffer, (void *)((uintptr_t)tmpnode->data + offset), size);

	VOP_UNLOCK(node);
	return 0;
}

static int tmpfs_write(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec, cred_t *cred) {
	tmpfsnode_t *tmpnode = (tmpfsnode_t *)node;
	if (node->type != V_TYPE_REGULAR)
		return EINVAL;

	size_t top = offset + size;

	VOP_LOCK(node);
	if (top > tmpnode->attr.size) {
		int err = resize(tmpnode, top);
		if (err) {
			VOP_UNLOCK(node);
			return err;
		}
	}

	*writec = size;
	memcpy((void *)((uintptr_t)tmpnode->data + offset), buffer, size);

	VOP_UNLOCK(node);
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
	VOP_LOCK(node);

	int err = hashtable_get(&tmpnode->children, &r, name, namelen);
	if (err) {
		VOP_UNLOCK(node);
		return err;
	}

	vnode_t *unlinknode = r;
	tmpfsnode_t *unlinktmpnode = r;

	err = hashtable_remove(&tmpnode->children, name, namelen);
	VOP_UNLOCK(node);
	if (err)
		return err;

	VOP_LOCK(unlinknode);
	--unlinktmpnode->attr.nlinks;
	VOP_UNLOCK(unlinknode);
	VOP_RELEASE(unlinknode);

	return 0;
}

static int tmpfs_link(vnode_t *node, vnode_t *dir, char *name, cred_t *cred) {
	if (node->vfs != dir->vfs)
		return EXDEV;

	if (dir->type != V_TYPE_DIR)
		return ENOTDIR;

	tmpfsnode_t *tmpdir = (tmpfsnode_t *)dir;
	size_t namelen = strlen(name);
	void *v;
	VOP_LOCK(dir);
	if (hashtable_get(&tmpdir->children, &v, name, namelen) == 0) {
		VOP_UNLOCK(node);
		return EEXIST;
	}

	int error = hashtable_set(&tmpdir->children, node, name, namelen, true);
	VOP_UNLOCK(dir);
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

	strcpy(pathbuf, path);

	vnode_t *linknode = NULL;
	int err = tmpfs_create(node, name, attr, V_TYPE_LINK, &linknode, cred);
	if (err) {
		free(pathbuf);
		return err;
	}

	VOP_LOCK(linknode);
	tmpfsnode_t *linktmpnode = (tmpfsnode_t *)linknode;
	linktmpnode->link = pathbuf;
	VOP_UNLOCK(linknode);
	VOP_RELEASE(linknode);

	return 0;
}

static int tmpfs_readlink(vnode_t *node, char **link, cred_t *cred) {
	if (node->type != V_TYPE_LINK)
		return EINVAL;

	tmpfsnode_t *tmpnode = (tmpfsnode_t *)node;

	VOP_LOCK(node);
	char *ret = alloc(strlen(tmpnode->link) + 1);
	if (ret == NULL) {
		VOP_UNLOCK(node);
		return ENOMEM;
	}

	strcpy(ret, tmpnode->link);
	*link = ret;

	VOP_UNLOCK(node);
	return 0;
}

#define MMAPTMPFLAGS (ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_WRITE)

static int tmpfs_mmap(vnode_t *node, void *addr, uintmax_t offset, int flags, cred_t *cred) {
	void *paddr = pmm_allocpage(PMM_SECTION_DEFAULT);
	if (paddr == NULL)
		return ENOMEM;

	if (arch_mmu_map(_cpu()->vmmctx->pagetable, paddr, addr, MMAPTMPFLAGS) == false) {
		pmm_release(paddr);
		return ENOMEM;
	}

	size_t readc;
	int e = tmpfs_read(node, addr, PAGE_SIZE, offset, flags, &readc, cred);
	if (e) {
		arch_mmu_unmap(_cpu()->vmmctx->pagetable, addr);
		pmm_release(paddr);
		return e;
	}

	memset((void *)((uintptr_t)addr + readc), 0, PAGE_SIZE - readc);

	arch_mmu_remap(_cpu()->vmmctx->pagetable, paddr, addr, vnodeflagstommuflags(flags));

	return 0;
}

static int tmpfs_munmap(vnode_t *node, void *addr, uintmax_t offset, int flags, cred_t *cred) {
	if (flags & V_FFLAGS_SHARED) {
		size_t wc;
		// TODO no growing flag
		__assert(tmpfs_write(node, addr, PAGE_SIZE, offset, flags, &wc, cred) == 0);
	}

	void *paddr = arch_mmu_getphysical(_cpu()->vmmctx->pagetable, addr);
	__assert(paddr);
	arch_mmu_unmap(_cpu()->vmmctx->pagetable, addr);
	pmm_release(paddr);
	return 0;
}


static int tmpfs_getdents(vnode_t *node, dent_t *buffer, size_t count, uintmax_t offset, size_t *readcount) {
	if (node->type != V_TYPE_DIR)
		return ENOTDIR;

	VOP_LOCK(node);

	tmpfsnode_t *tmpnode = (tmpfsnode_t *)node;

	*readcount = 0;
	size_t current = 0;

	HASHTABLE_FOREACH(&tmpnode->children) {
		if (current < offset) {
			++current;
			continue;
		}

		if (*readcount == count)
			break;

		dent_t *ent = &buffer[*readcount];
		tmpfsnode_t *itmpnode = entry->value;

		ent->d_ino = itmpnode->attr.inode;
		ent->d_off = offset;
		ent->d_reclen = sizeof(dent_t);
		ent->d_type = vfs_getposixtype(itmpnode->vnode.type);
		strcpy(ent->d_name, entry->key);

		*readcount += 1;
	}

	VOP_UNLOCK(node);
	return 0;
}

static int tmpfs_inactive(vnode_t *node) {
	freenode((tmpfsnode_t *)node);
	return 0;
}

static int tmpfs_resize(vnode_t *node, size_t size, cred_t *) {
	VOP_LOCK(node);

	int err = resize((tmpfsnode_t *)node, size);

	VOP_UNLOCK(node);
	return err;
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
	.poll = vfs_pollstub,
	.read = tmpfs_read,
	.write = tmpfs_write,
	.access = tmpfs_access,
	.unlink = tmpfs_unlink,
	.link = tmpfs_link,
	.symlink = tmpfs_symlink,
	.readlink = tmpfs_readlink,
	.inactive = tmpfs_inactive,
	.mmap = tmpfs_mmap,
	.munmap = tmpfs_munmap,
	.getdents = tmpfs_getdents,
	.resize = tmpfs_resize
};

static tmpfsnode_t *newnode(vfs_t *vfs, int type) {
	tmpfsnode_t *node = slab_allocate(nodecache);
	if (node == NULL)
		return NULL;

	memset(node, 0, sizeof(tmpfsnode_t));

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

	node->attr.type = type;
	node->attr.inode = ++((tmpfs_t *)vfs)->inodenumber;
	VOP_INIT(&node->vnode, &vnops, 0, type, vfs);
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
