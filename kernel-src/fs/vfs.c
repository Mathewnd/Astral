#include <kernel/vfs.h>
#include <hashtable.h>
#include <string.h>
#include <logging.h>
#include <kernel/alloc.h>
#include <errno.h>
#include <arch/cpu.h>
#include <kernel/poll.h>
#include <kernel/sock.h>
#include <kernel/vmmcache.h>
#include <kernel/block.h>
#include <kernel/pipefs.h>
#include <kernel/auth.h>

#define PATHNAME_MAX 512
#define MAXLINKDEPTH 64

static hashtable_t fstable;
vnode_t *vfsroot;

static spinlock_t listlock;
static vfs_t *vfslist;

static cred_t *getcred() {
	if (_cpu()->thread == NULL || _cpu()->thread->proc == NULL)
		return NULL;
	else
		return &_cpu()->thread->proc->cred;
}

int vfs_pollstub(vnode_t *node, struct polldata *, int events) {
	int revents = 0;

	if (events & POLLIN)
		revents |= POLLIN;

	if (events & POLLOUT)
		revents |= POLLOUT;

	return revents;
}

void vfs_init() {
	__assert(hashtable_init(&fstable, 20) == 0);
	vfsroot = alloc(sizeof(vnode_t));
	__assert(vfsroot);
	SPINLOCK_INIT(listlock);
	vfsroot->type = V_TYPE_DIR;
	vfsroot->refcount = 1;
}

int vfs_register(vfsops_t *ops, char *name) {
	return hashtable_set(&fstable, ops, name, strlen(name), true);
}

void vfs_inactive(vnode_t *vnode) {
	if (vnode->type == V_TYPE_SOCKET && vnode->socketbinding) {
		localsock_leavebinding(vnode);
	} else if (vnode->type == V_TYPE_FIFO) {
		pipefs_leavebinding(vnode);
	}
	vnode->ops->inactive(vnode);
}

// backing and pathref are expected to be unlocked
int vfs_mount(vnode_t *backing, vnode_t *pathref, char *path, char *name, void *data) {
	vfsops_t *ops;
	int err = hashtable_get(&fstable, (void **)(&ops), name, strlen(name));
	if (err)
		return err;

	vnode_t *mounton;
	err = vfs_lookup(&mounton, pathref, path, NULL, 0);

	if (err)
		return err;

	err = auth_filesystem_check(getcred(), AUTH_ACTIONS_FILESYSTEM_MOUNT, mounton);
	if (err) {
		// locked by vfs_lookup
		VOP_UNLOCK(mounton);
		VOP_RELEASE(mounton);
		return err;
	}

	if (mounton->type != V_TYPE_DIR) {
		// locked by vfs_lookup
		VOP_UNLOCK(mounton);
		VOP_RELEASE(mounton);
		return ENOTDIR;
	}

	vfs_t *vfs;

	err = ops->mount(&vfs, mounton, backing, data);

	if (err) {
		// locked by vfs_lookup
		VOP_UNLOCK(mounton);
		VOP_RELEASE(mounton);
		return err;
	}

	spinlock_acquire(&listlock);

	vfs->next = vfslist;
	vfslist = vfs;

	spinlock_release(&listlock);

	mounton->vfsmounted = vfs;
	vfs->nodecovered = mounton;

	// locked by vfs_lookup
	VOP_UNLOCK(mounton);

	return 0;
}

// ref is expected to be unlocked
int vfs_open(vnode_t *ref, char *path, int flags, vnode_t **res) {
	vnode_t *tmp = NULL;
	int err = vfs_lookup(&tmp, ref, path, NULL, 0);
	if (err)
		return err;

	err = VOP_ACCESS(tmp, ((flags & V_FFLAGS_READ) ? V_ACCESS_READ : 0) | ((flags & V_FFLAGS_WRITE) ? V_ACCESS_WRITE : 0), getcred());
	if (err) {
		// locked by vfs_lookup
		VOP_UNLOCK(tmp);
		return err;
	}

	vnode_t *new = ref;
	err = VOP_OPEN(&new, flags, getcred());
	if (err) {
		VOP_RELEASE(tmp);
	} else {
		*res = new != tmp ? new : tmp;
	}

	// locked by vfs_lookup
	VOP_UNLOCK(tmp);

	return err;
}

// node is expected to be unlocked
int vfs_close(vnode_t *node, int flags) {
	VOP_LOCK(node);
	int err = VOP_CLOSE(node, flags, getcred());
	VOP_UNLOCK(node);
	return err;
}

// if node is not NULL, then a reference is kept and the newnode is returned in node
// ref is expected to be unlocked.
// the new node is returned locked
int vfs_create(vnode_t *ref, char *path, vattr_t *attr, int type, vnode_t **node) {
	vnode_t *parent;
	char *component = alloc(strlen(path) + 1);
	int err = vfs_lookup(&parent, ref, path, component, VFS_LOOKUP_PARENT);
	if (err)
		goto cleanup;

	err = VOP_ACCESS(parent, V_ACCESS_WRITE, getcred());
	if (err) {
		VOP_UNLOCK(parent);
		VOP_RELEASE(parent);
		goto cleanup_parent;
	}

	vnode_t *ret;
	err = VOP_CREATE(parent, component, attr, type, &ret, getcred());
	VOP_RELEASE(parent);
	if (err) {
		VOP_UNLOCK(parent);
		goto cleanup;
	}

	if (node) {
		*node = ret;
	} else {
		// returned locked by VOP_CREATE
		VOP_UNLOCK(ret);
		VOP_RELEASE(ret);
	}

	cleanup_parent:
	VOP_UNLOCK(parent);
	cleanup:
	free(component);
	return err;
}

static void bytestopages(uintmax_t offset, size_t size, uintmax_t *pageoffset, size_t *pagecount, uintmax_t *startoffset) {
	*pageoffset = offset / PAGE_SIZE;
	*startoffset = offset % PAGE_SIZE;
	size_t end = offset + size;
	size_t toppage = ROUND_UP(end, PAGE_SIZE) / PAGE_SIZE;
	*pagecount = toppage - *pageoffset;
}

static int writenocache(vnode_t *node, page_t *page, uintmax_t pageoffset) {
	// don't keep the pages in the cache!
	// if its a file, do the proper filesystem sync.
	// if its a block device, sync only the page we just dirtied
	int e;
	if (node->type == V_TYPE_REGULAR)
		e = VOP_SYNC(node);
	else
		e = vmmcache_syncvnode(node, pageoffset, PAGE_SIZE);

	// try to turn it into anonymous memory
	vmmcache_evict(page);

	return e;
}

int vfs_write(vnode_t *node, void *buffer, size_t size, uintmax_t offset, size_t *written, int flags) {
	VOP_LOCK(node);
	int err = 0;
	if (node->type == V_TYPE_REGULAR || node->type == V_TYPE_BLKDEV) {
		*written = 0;
		// can't write a size 0 buffer
		if (size == 0)
			goto leave;

		// overflow
		if (size + offset < offset) {
			err = EINVAL;
			goto leave;
		}

		vattr_t attr;
		err = VOP_GETATTR(node, &attr, getcred());
		if (err)
			goto leave;

		size_t newsize = size + offset > attr.size ? size + offset : 0;

		if (node->type == V_TYPE_REGULAR && newsize) {
			// do resize stuff if regular and applicable
			err = VOP_RESIZE(node, newsize, &_cpu()->thread->proc->cred);
			if (err)
				goto leave;
		} else if (node->type == V_TYPE_BLKDEV) {
			// else just get the disk size and limit the read size
			blockdesc_t blockdesc;
			int r;
			err = VOP_IOCTL(node, BLOCK_IOCTL_GETDESC, &blockdesc, &r);
			__assert(err == 0);

			size_t bytesize = blockdesc.blockcapacity * blockdesc.blocksize;

			if (offset >= bytesize)
				goto leave;

			size = min(size + offset, bytesize) - offset;
		}

		uintmax_t pageoffset, pagecount, startoffset;
		bytestopages(offset, size, &pageoffset, &pagecount, &startoffset);
		page_t *page = NULL;

		if (startoffset) {
			// unaligned first page
			err = vmmcache_getpage(node, pageoffset * PAGE_SIZE, &page);
			if (err)
				goto leave;

			size_t writesize = min(PAGE_SIZE - startoffset, size);
			void *address = MAKE_HHDM(pmm_getpageaddress(page));
			memcpy((void *)((uintptr_t)address + startoffset), buffer, writesize);
			vmmcache_makedirty(page);
			*written += writesize;
			pageoffset += 1;
			pagecount -= 1;

			if (flags & V_FFLAGS_NOCACHE)
				err = writenocache(node, page, pageoffset * PAGE_SIZE);

			pmm_release(FROM_HHDM(address));
			if (err)
				goto leave;
		}

		for (uintmax_t offset = 0; offset < pagecount * PAGE_SIZE; offset += PAGE_SIZE) {
			// the other pages
			err = vmmcache_getpage(node, pageoffset * PAGE_SIZE + offset, &page);
			if (err)
				goto leave;

			size_t writesize = min(PAGE_SIZE, size - *written);
			void *address = MAKE_HHDM(pmm_getpageaddress(page));
			memcpy(address, (void *)((uintptr_t)buffer + *written), writesize);
			vmmcache_makedirty(page);
			*written += writesize;

			if (flags & V_FFLAGS_NOCACHE)
				err = writenocache(node, page, pageoffset * PAGE_SIZE + offset);

			pmm_release(FROM_HHDM(address));

			if (err)
				goto leave;
		}
	} else {
		// special file, just write as its not being cached
		err = VOP_WRITE(node, buffer, size, offset, flags, written, getcred());
	}

	leave:
	VOP_UNLOCK(node);
	return err;
}

int vfs_read(vnode_t *node, void *buffer, size_t size, uintmax_t offset, size_t *bytesread, int flags) {
	VOP_LOCK(node);
	int err = 0;
	if (node->type == V_TYPE_REGULAR || node->type == V_TYPE_BLKDEV) {
		*bytesread = 0;
		// can't read 0 bytes from the cache
		if (size == 0)
			goto leave;

		// overflow
		if (size + offset < offset) {
			err = EINVAL;
			goto leave;
		}

		size_t nodesize = 0;

		if (node->type == V_TYPE_REGULAR) {
			vattr_t attr;
			err = VOP_GETATTR(node, &attr, getcred());
			if (err)
				goto leave;

			nodesize = attr.size;
		} else {
			blockdesc_t blockdesc;
			int r;
			err = VOP_IOCTL(node, BLOCK_IOCTL_GETDESC, &blockdesc, &r);
			__assert(err == 0);

			nodesize = blockdesc.blockcapacity * blockdesc.blocksize;
		}

		// read past end of file?
		if (offset >= nodesize)
			goto leave;

		size = min(size + offset, nodesize) - offset;

		uintmax_t pageoffset, pagecount, startoffset;
		bytestopages(offset, size, &pageoffset, &pagecount, &startoffset);
		page_t *page = NULL;

		if (startoffset) {
			// unaligned first page
			err = vmmcache_getpage(node, pageoffset * PAGE_SIZE, &page);
			if (err)
				goto leave;

			size_t readsize = min(PAGE_SIZE - startoffset, size);
			void *address = MAKE_HHDM(pmm_getpageaddress(page));
			memcpy(buffer, (void *)((uintptr_t)address + startoffset), readsize);
			*bytesread += readsize;
			pageoffset += 1;
			pagecount -= 1;

			if (flags & V_FFLAGS_NOCACHE) {
				// try to turn it into anonymous memory
				vmmcache_evict(page);
			}
			pmm_release(FROM_HHDM(address));
		}

		for (uintmax_t offset = 0; offset < pagecount * PAGE_SIZE; offset += PAGE_SIZE) {
			// the other pages
			err = vmmcache_getpage(node, pageoffset * PAGE_SIZE + offset, &page);
			if (err)
				goto leave;

			size_t readsize = min(PAGE_SIZE, size - *bytesread);
			void *address = MAKE_HHDM(pmm_getpageaddress(page));
			memcpy((void *)((uintptr_t)buffer + *bytesread), address, readsize);
			*bytesread += readsize;
			if (flags & V_FFLAGS_NOCACHE) {
				// try to turn it into anonymous memory
				vmmcache_evict(page);
			}
			pmm_release(FROM_HHDM(address));
		}
	} else {
		// special file, just read as size doesn't matter
		err = VOP_READ(node, buffer, size, offset, flags, bytesread, getcred());
	}
	leave:
	VOP_UNLOCK(node);
	return err;
}

// if type is V_TYPE_LINK, a symlink is made
// in that case, destref is ignored, destpath is the link value and attr points to the attributes of the symlink
// if type is V_TYPE_REGULAR, a hardlink is made.
// in that case, destref and destpath point to the the node that will be linked and attr is ignored
// in both cases, linkref and linkpath describe the location of where to create the new link
int vfs_link(vnode_t *destref, char *destpath, vnode_t *linkref, char *linkpath, int type, vattr_t *attr) {
	__assert(type == V_TYPE_LINK || type == V_TYPE_REGULAR);
	char *component = alloc(strlen(linkpath) + 1);
	vnode_t *parent = NULL;
	int err = vfs_lookup(&parent, linkref, linkpath, component, VFS_LOOKUP_PARENT);
	if (err)
		goto cleanup;

	err = VOP_ACCESS(parent, V_ACCESS_WRITE, getcred());
	if (err) {
		// locked by vfs_lookup
		VOP_UNLOCK(parent);
		goto cleanup;
	}

	// TODO rewrite this as it can deadlock as a directory is locked
	if (type == V_TYPE_REGULAR) {
		vnode_t *targetnode = NULL;
		err = vfs_lookup(&targetnode, destref, destpath, NULL, 0);
		if (err) {
			// locked by vfs_lookup
			VOP_UNLOCK(parent);
			goto cleanup;
		}
		err = VOP_LINK(targetnode, parent, component, getcred());
		// locked by vfs_lookup
		VOP_UNLOCK(targetnode);
		VOP_RELEASE(targetnode);
	} else {
		err = VOP_SYMLINK(parent, component, attr, destpath, getcred());
	}

	// locked by vfs_lookup
	VOP_UNLOCK(parent);
	cleanup:
	VOP_RELEASE(parent);
	free(component);
	return err;
}

int vfs_unlink(vnode_t *ref, char *path) {
	char *component = alloc(strlen(path) + 1);
	vnode_t *parent = NULL;
	int err = vfs_lookup(&parent, ref, path, component, VFS_LOOKUP_PARENT);
	if (err)
		goto cleanup;

	VOP_UNLINK(parent, component, getcred());
	// locked by vfs_lookup
	VOP_UNLOCK(parent);
	VOP_RELEASE(parent);

	cleanup:
	free(component);
	return err;
}

// returns the highest node in a mount point
static int highestnodeinmp(vnode_t *node, vnode_t **ret) {
	int e = 0;
	while (e == 0 && node->vfsmounted)
		e = VFS_ROOT(node->vfsmounted, &node);

	*ret = node;

	return e;
}

// returns the lowest node in a mount point
static vnode_t *lowestnodeinmp(vnode_t *node) {
	while (node->flags & V_FLAGS_ROOT)
		node = node->vfs->nodecovered;

	return node;
}

// looks up a pathname
// result will always be returned locked.
// if flags & VFS_LOOKUP_INPUTLOCKED, the input is already locked and will be locked on return
// if flags & VFS_LOOKUP_PARENT, the parent of the resulting node will be put in
// the result pointer and lastcomp will contain the last component name
// if flags & VFS_LOOKUP_NOLINK, the last component will not be dereferenced if its a symbolic link
// if flags & VFS_LOOKUP_INTERNAL, the first byte of flags will be the current count of symlinks transversed
// this is not an external flag and is meant to be called from inside the function itself
int vfs_lookup(vnode_t **result, vnode_t *start, char *path, char *lastcomp, int flags) {
	if ((flags & VFS_LOOKUP_INTERNAL) && (flags & 0xff) > MAXLINKDEPTH)
		return ELOOP;

	size_t pathlen = strlen(path);

	if (pathlen == 0) {
		if (flags & VFS_LOOKUP_PARENT) {
			return ENOENT;
		}

		vnode_t *r = start;
		int e = highestnodeinmp(start, &r);
		if (e)
			return e;

		if ((flags & VFS_LOOKUP_INPUTLOCKED) == 0) {
			VOP_LOCK(r);
		}

		VOP_HOLD(r);
		*result = r;
		return 0;
	}

	if (pathlen > PATHNAME_MAX)
		return ENAMETOOLONG;

	vnode_t *current = start;
	int error = highestnodeinmp(start, &current);
	if (error)
		return error;

	char *compbuffer = alloc(pathlen + 1);
	if (compbuffer == NULL)
		return ENOMEM;

	strcpy(compbuffer, path);

	for (int i = 0; i < pathlen; ++i) {
		if (compbuffer[i] == '/')
			compbuffer[i] = '\0';
	}

	vnode_t *next;
	error = 0;
	VOP_HOLD(current);

	if ((flags & VFS_LOOKUP_INPUTLOCKED) == 0 || current != start)
		VOP_LOCK(current);

	for (int i = 0; i < pathlen; ++i) {
		if (compbuffer[i] == '\0')
			continue;

		if (current->type != V_TYPE_DIR) {
			error = ENOTDIR;
			break;
		}

		char *component = &compbuffer[i];
		size_t complen = strlen(component);
		bool islast = i + complen == pathlen;

		// check if its last with trailing '/' (or '\0's in this case, as '/'s were turned into '\0's in the buffer)
		if (!islast) {
			int j;
			for (j = i + complen; j < pathlen && compbuffer[j] == '\0'; ++j) asm("");
			islast = j == pathlen;
		}

		if (islast && (flags & VFS_LOOKUP_PARENT)) {
			__assert(lastcomp);
			strcpy(lastcomp, component);
			break;
		}

		if (strcmp(component, "..") == 0) {
			// if the tree root, skip to next component
			vnode_t *root = NULL;
			__assert(highestnodeinmp(vfsroot, &root) == 0);
			if (root == current) {
				i += complen;
				continue;
			}

			// if the root of a mounted fs, go to
			if (current->flags & V_FLAGS_ROOT) {
				vnode_t *n = lowestnodeinmp(current);
				if (n != current) {
					VOP_HOLD(n);
					VOP_UNLOCK(current);
					VOP_RELEASE(current);
					current = n;
					VOP_LOCK(current);
				}
			}
		}

		// locked at the beggining of the function if nescessary or by a previous VOP_LOOKUP
		error = VOP_ACCESS(current, V_ACCESS_SEARCH, getcred());
		if (error)
			break;

		error = VOP_LOOKUP(current, component, &next, getcred());
		if (error)
			break;

		if ((current != start || (flags & VFS_LOOKUP_INPUTLOCKED) == 0) && current != next)
			VOP_UNLOCK(current);

		vnode_t *r = next;
		error = highestnodeinmp(next, &r);
		if (error) {
			if (current != next)
				VOP_UNLOCK(next);

			VOP_RELEASE(next);
			break;
		}

		if (r != next) {
			VOP_HOLD(r);
			VOP_UNLOCK(next);
			VOP_RELEASE(next);
			next = r;
			VOP_LOCK(next);
		}

		// dereference symlink
		if (next->type == V_TYPE_LINK && (islast == false || (islast == true && (flags & VFS_LOOKUP_NOLINK) == 0))) {
			// get path
			char *linkderef;

			// next is already returned locked by VOP_LOOKUP
			error = VOP_READLINK(next, &linkderef, getcred());
			VOP_UNLOCK(next);
			if (error) {
				VOP_RELEASE(next);
				break;
			}

			// lookup new path with recursion
			int pass = flags & VFS_LOOKUP_INTERNAL ? flags + 1 : VFS_LOOKUP_INTERNAL;
			vnode_t *derefresult = NULL;

			// get the start node of the dereference
			vnode_t *derefstart = current;

			if (*linkderef == '/' && _cpu()->thread && _cpu()->thread->proc) {
				MUTEX_ACQUIRE(&_cpu()->thread->proc->mutex, false);
				derefstart = _cpu()->thread->proc->root;
				VOP_HOLD(derefstart);
				MUTEX_RELEASE(&_cpu()->thread->proc->mutex);
			} else if (*linkderef == '/') {
				derefstart = vfsroot;
				VOP_HOLD(derefstart);
			}

			// TODO possible deadlock when input vnode is locked and the vfs_lookup would lock it
			error = vfs_lookup(&derefresult, derefstart, linkderef, NULL, pass);

			if (*linkderef == '/')
				VOP_RELEASE(derefstart);

			free(linkderef);

			VOP_RELEASE(next);
			if (error) {
				break;
			}

			next = derefresult;
		}

		VOP_RELEASE(current);
		current = next;
		i += complen;
	}

	if (error) {
		// current is locked here, unlock it if not the start or start wasnt locked
		if (current != start || (flags & VFS_LOOKUP_INPUTLOCKED) == 0)
			VOP_UNLOCK(current);

		VOP_RELEASE(current);
	} else {
		// result is already locked here
		*result = current;
	}

	free(compbuffer);
	return error;
}
