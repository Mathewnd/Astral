#ifndef _VFS_H
#define _VFS_H

#include <spinlock.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/abi.h>
#include <time.h>

typedef struct {
	uid_t uid;
	gid_t gid;
} cred_t;

typedef struct {
	int type;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	int fsid;
	ino_t inode;
	int nlinks;
	size_t size;
	size_t fsblocksize;
	timespec_t atime;
	timespec_t mtime;
	timespec_t ctime;
	int rdev;
	size_t blocksused;
} vattr_t;

typedef struct vfs_t {
	struct vfs_t *next;
	struct vfsops_t *ops;
	struct vnode_t *nodecovered;
	struct vnode_t *root;
	int flags;
} vfs_t;

#define V_FLAGS_ROOT 1

#define V_TYPE_REGULAR	0
#define V_TYPE_DIR	1
#define V_TYPE_CHDEV	2
#define V_TYPE_BLKDEV	3
#define V_TYPE_FIFO	4
#define V_TYPE_LINK	5
#define V_TYPE_SOCKET	6

#define V_FFLAGS_READ 1
#define V_FFLAGS_WRITE 2
#define V_FFLAGS_NONBLOCKING 4

typedef struct vnode_t {
	struct vops_t *ops;
	spinlock_t lock;
	int refcount;
	int flags;
	int type;
	vfs_t *vfs;
	vfs_t *vfsmounted;
} vnode_t;

typedef struct vfsops_t {
	int (*mount)(vfs_t **vfs, vnode_t *mp, vnode_t *backing, void *data);
	int (*unmount)(vfs_t *vfs);
	int (*sync)(vfs_t *vfs);
	int (*root)(vfs_t *vfs, vnode_t **root);
} vfsops_t;

typedef struct vops_t {
	int (*open)(vnode_t **node, int flags, cred_t *cred);
	int (*close)(vnode_t *node, int flags, cred_t *cred);
	int (*read)(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc, cred_t *cred);
	int (*write)(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec, cred_t *cred);
	int (*lookup)(vnode_t *node, char *name, vnode_t **result, cred_t *cred);
	int (*create)(vnode_t *parent, char *name, vattr_t *attr, int type, vnode_t **result, cred_t *cred);
	int (*getattr)(vnode_t *node, vattr_t *attr, cred_t *cred);
	int (*setattr)(vnode_t *node, vattr_t *attr, cred_t *cred);
	int (*poll)(vnode_t *node, int events);
	int (*access)(vnode_t *node, mode_t mode, cred_t *cred);
	int (*unlink)(vnode_t *node, char *name, cred_t *cred);
	int (*link)(vnode_t *node, vnode_t *dir, char *name, cred_t *cred);
	int (*symlink)(vnode_t *parent, char *name, vattr_t *attr, char *path, cred_t *cred);
	int (*readlink)(vnode_t *parent, char **link, cred_t *cred);
	int (*inactive)(vnode_t *node);
} vops_t;

#define VFS_MOUNT(vfs, mp, b, d) (vfs)->ops->mount(vfs, mp, b, d)
#define VFS_UNMOUNT(vfs) (vfs)->ops->unmount(vfs)
#define VFS_ROOT(vfs, r) (vfs)->ops->root(vfs, r)
#define VFS_SYNC(vfs) (vfs)->ops->sync(vfs)

#define VOP_LOCK(v) spinlock_acquire(&(v)->lock)
#define VOP_UNLOCK(v) spinlock_release(&(v)->lock)

// these are expected to be called with the vnode(s) locked
#define VOP_OPEN(v, f, c) (*v)->ops->open(v, f, c)
#define VOP_CLOSE(v, f, c) (v)->ops->close(v, f, c)
#define VOP_READ(v, b, s, o, f, r, c) (v)->ops->read(v, b, s, o, f, r, c)
#define VOP_WRITE(v, b, s, o, f, r, c) (v)->ops->write(v, b, s, o, f, r, c)
#define VOP_LOOKUP(v, n, r, c) (v)->ops->lookup(v, n, r, c)
#define VOP_CREATE(v, n, a, t, r, c) (v)->ops->create(v, n, a, t, r, c)
#define VOP_GETATTR(v, a, c) (v)->ops->getattr(v, a, c)
#define VOP_SETATTR(v, a, c) (v)->ops->setattr(v, a, c)
#define VOP_ACCESS(v, m, c) (v)->ops->access(v, m, c)
#define VOP_UNLINK(v, n, c) (v)->ops->unlink(v, n, c)
#define VOP_LINK(v, d, n, c) (v)->ops->link(v, d, n, c)
#define VOP_SYMLINK(v, n, a, p, c) (v)->ops->symlink(v, n, a, p, c)
#define VOP_READLINK(v, l, c) (v)->ops->readlink(v, l, c)

// lock not needed unless atomicity is desired
#define VOP_POLL(v, p) (v)->ops->poll(v, p);
#define VOP_HOLD(v) __atomic_add_fetch(&(v)->refcount, 1, __ATOMIC_SEQ_CST)
#define VOP_RELEASE(v) {\
		if (__atomic_sub_fetch(&(v)->refcount, 1, __ATOMIC_SEQ_CST) == 0) {\
			(v)->ops->inactive(v); \
			v = NULL; \
		} \
	}

extern vnode_t *vfsroot;

void vfs_init();
int vfs_mount(vnode_t *backing, vnode_t *pathref, char *path, char *name, void *data);
int vfs_register(vfsops_t *vfsops, char *name);

int vfs_open(vnode_t *ref, char *path, int flags, vnode_t **result);
int vfs_close(vnode_t *node, int flags);
int vfs_write(vnode_t *node, void *buffer, size_t size, uintmax_t offset, size_t *written, int flags);
int vfs_read(vnode_t *node, void *buffer, size_t size, uintmax_t offset, size_t *bytesread, int flags);
int vfs_create(vnode_t *ref, char *path, vattr_t *attr, int type, vnode_t **node);

#define VFS_LOOKUP_PARENT 1
#define VFS_LOOKUP_NOLINK 2
#define VFS_LOOKUP_INTERNAL 0x80000000
int vfs_lookup(vnode_t **result, vnode_t *start, char *path, char *lastcomp, int flags);

#endif
