#ifndef _VFS_H
#define _VFS_H

#include <mutex.h>
#include <spinlock.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/abi.h>
#include <time.h>
#include <errno.h>
#include <kernel/cred.h>

#define V_ATTR_MODE	1
#define V_ATTR_UID	2
#define V_ATTR_GID	4
#define V_ATTR_ATIME	8
#define V_ATTR_MTIME	16
#define V_ATTR_CTIME	32

#define V_ATTR_ALL (V_ATTR_MODE | V_ATTR_UID | V_ATTR_GID | V_ATTR_ATIME | V_ATTR_MTIME | V_ATTR_CTIME)

#define V_ATTR_MODE_OTHERS_EXECUTE 	00001
#define V_ATTR_MODE_OTHERS_SEARCH 	V_ATTR_MODE_OTHERS_EXECUTE
#define V_ATTR_MODE_OTHERS_WRITE 	00002
#define V_ATTR_MODE_OTHERS_READ 	00004
#define V_ATTR_MODE_OTHERS_ALL		00007
#define V_ATTR_MODE_GROUP_EXECUTE 	00010
#define V_ATTR_MODE_GROUP_SEARCH 	V_ATTR_MODE_GROUP_EXECUTE
#define V_ATTR_MODE_GROUP_WRITE 	00020
#define V_ATTR_MODE_GROUP_READ 		00040
#define V_ATTR_MODE_GROUP_ALL		00070
#define V_ATTR_MODE_USER_EXECUTE 	00100
#define V_ATTR_MODE_USER_SEARCH 	V_ATTR_MODE_USER_EXECUTE
#define V_ATTR_MODE_USER_WRITE 		00200
#define V_ATTR_MODE_USER_READ 		00400
#define V_ATTR_MODE_USER_ALL		00700
#define V_ATTR_MODE_STICKY 		01000
#define V_ATTR_MODE_SGID 		02000
#define V_ATTR_MODE_SUID 		04000

#define V_ACCESS_SEARCH 1
#define V_ACCESS_EXECUTE 1
#define V_ACCESS_WRITE 2
#define V_ACCESS_READ 4

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
	int rdevmajor;
	int rdevminor;
	int devmajor;
	int devminor;
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
#define V_FFLAGS_SHARED 8
#define V_FFLAGS_EXEC 16
#define V_FFLAGS_NOCTTY 32
#define V_FFLAGS_NOCACHE 64

typedef struct vnode_t {
	struct vops_t *ops;
	mutex_t lock;
	mutex_t sizelock;
	int refcount;
	int flags;
	int type;
	vfs_t *vfs;
	vfs_t *vfsmounted;
	union {
		void *socketbinding;
		void *fifobinding;
	};

	struct page_t *pages;
} vnode_t;

typedef struct vfsops_t {
	int (*mount)(vfs_t **vfs, vnode_t *mp, vnode_t *backing, void *data);
	int (*unmount)(vfs_t *vfs);
	int (*sync)(vfs_t *vfs);
	int (*root)(vfs_t *vfs, vnode_t **root);
} vfsops_t;

struct polldata;

typedef struct vops_t {
	int (*open)(vnode_t **node, int flags, cred_t *cred);
	int (*close)(vnode_t *node, int flags, cred_t *cred);
	int (*read)(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc, cred_t *cred);
	int (*write)(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec, cred_t *cred);
	int (*lookup)(vnode_t *node, char *name, vnode_t **result, cred_t *cred);
	int (*create)(vnode_t *parent, char *name, vattr_t *attr, int type, vnode_t **result, cred_t *cred);
	int (*getattr)(vnode_t *node, vattr_t *attr, cred_t *cred);
	int (*setattr)(vnode_t *node, vattr_t *attr, int attrs, cred_t *cred);
	int (*poll)(vnode_t *node, struct polldata *, int events);
	int (*access)(vnode_t *node, mode_t mode, cred_t *cred);
	int (*unlink)(vnode_t *node, char *name, cred_t *cred);
	int (*link)(vnode_t *node, vnode_t *dir, char *name, cred_t *cred);
	int (*symlink)(vnode_t *parent, char *name, vattr_t *attr, char *path, cred_t *cred);
	int (*readlink)(vnode_t *parent, char **link, cred_t *cred);
	int (*inactive)(vnode_t *node);
	int (*mmap)(vnode_t *node, void *addr, uintmax_t offset, int flags, cred_t *cred);
	int (*munmap)(vnode_t *node, void *addr, uintmax_t offset, int flags, cred_t *cred);
	int (*getdents)(vnode_t *node, dent_t *buffer, size_t count, uintmax_t offset, size_t *readcount);
	int (*isatty)(vnode_t *node);
	int (*ioctl)(vnode_t *node, unsigned long request, void *arg, int *result);
	int (*maxseek)(vnode_t *node, size_t *max);
	int (*resize)(vnode_t *node, size_t newsize, cred_t *cred);
	int (*rename)(vnode_t *source, char *oldname, vnode_t *target, char *newname, int flags);
	int (*getpage)(vnode_t *node, uintmax_t offset, struct page_t *page);
	int (*putpage)(vnode_t *node, uintmax_t offset, struct page_t *page);
	int (*sync)(vnode_t *node);
} vops_t;

#define VFS_INIT(v, o, f) \
	(v)->next = NULL; \
	(v)->ops = o; \
	(v)->nodecovered = NULL; \
	(v)->root = NULL; \
	(v)->flags = f;

#define VFS_MOUNT(vfs, mp, b, d) (vfs)->ops->mount(vfs, mp, b, d)
#define VFS_UNMOUNT(vfs) (vfs)->ops->unmount(vfs)
#define VFS_ROOT(vfs, r) (vfs)->ops->root(vfs, r)
#define VFS_SYNC(vfs) (vfs)->ops->sync(vfs)

#define VOP_INIT(vn, o, f, t, v) \
	(vn)->ops = o; \
	MUTEX_INIT(&(vn)->lock); \
	MUTEX_INIT(&(vn)->sizelock); \
	(vn)->refcount = 1; \
	(vn)->flags = f; \
	(vn)->type = t; \
	(vn)->vfs = v; \
	(vn)->vfsmounted = NULL; 

#define VOP_LOCK(v) MUTEX_ACQUIRE(&(v)->lock, false)
#define VOP_UNLOCK(v) MUTEX_RELEASE(&(v)->lock)

#define VOP_OPEN(v, f, c) (*v)->ops->open(v, f, c)
#define VOP_CLOSE(v, f, c) (v)->ops->close(v, f, c)
#define VOP_READ(v, b, s, o, f, r, c) (v)->ops->read(v, b, s, o, f, r, c)
#define VOP_WRITE(v, b, s, o, f, r, c) (v)->ops->write(v, b, s, o, f, r, c)
#define VOP_LOOKUP(v, n, r, c) (v)->ops->lookup(v, n, r, c)
#define VOP_CREATE(v, n, a, t, r, c) (v)->ops->create(v, n, a, t, r, c)
#define VOP_GETATTR(v, a, c) (v)->ops->getattr(v, a, c)
#define VOP_SETATTR(v, a, w, c) (v)->ops->setattr(v, a, w, c)
#define VOP_ACCESS(v, m, c) (v)->ops->access(v, m, c)
#define VOP_UNLINK(v, n, c) (v)->ops->unlink(v, n, c)
#define VOP_LINK(v, d, n, c) (v)->ops->link(v, d, n, c)
#define VOP_SYMLINK(v, n, a, p, c) (v)->ops->symlink(v, n, a, p, c)
#define VOP_READLINK(v, l, c) (v)->ops->readlink(v, l, c)
#define VOP_MMAP(v, a, o, f, c) (v)->ops->mmap(v, a, o, f, c)
#define VOP_MUNMAP(v, a, o, f, c) (v)->ops->munmap(v, a, o, f, c)
#define VOP_GETDENTS(v, b, c, o, rc) (v)->ops->getdents(v, b, c, o, rc)
#define VOP_POLL(v, d, p) (v)->ops->poll(v, d, p)
#define VOP_ISATTY(v) ((v)->ops->isatty ? (v)->ops->isatty(v) : ENOTTY)
#define VOP_IOCTL(v, r, a, rp) ((v)->ops->ioctl ? (v)->ops->ioctl(v, r, a, rp) : ENOTTY)
#define VOP_MAXSEEK(v, rp) ((v)->ops->maxseek ? (v)->ops->maxseek(v, rp) : ENOTTY)
#define VOP_RESIZE(v, s, c) (v)->ops->resize(v, s, c)
#define VOP_RENAME(s, o, t, n, f) (s)->ops->rename(s, o, t, n, f)
#define VOP_GETPAGE(v, o, p) (v)->ops->getpage(v, o, p)
#define VOP_PUTPAGE(v, o, p) (v)->ops->putpage(v, o, p)
#define VOP_SYNC(v) (v)->ops->sync(v)
#define VOP_HOLD(v) __atomic_add_fetch(&(v)->refcount, 1, __ATOMIC_SEQ_CST)
#define VOP_RELEASE(v) {\
		if (__atomic_sub_fetch(&(v)->refcount, 1, __ATOMIC_SEQ_CST) == 0) {\
			vfs_inactive(v); \
			(v) = NULL; \
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
int vfs_link(vnode_t *destref, char *destpath, vnode_t *linkref, char *linkpath, int type, vattr_t *attr);
int vfs_unlink(vnode_t *ref, char *path);
int vfs_pollstub(vnode_t *node, struct polldata *, int events);
void vfs_inactive(vnode_t *node);

#define VFS_LOOKUP_PARENT 0x20000000
#define VFS_LOOKUP_NOLINK 0x40000000
#define VFS_LOOKUP_INTERNAL 0x80000000
int vfs_lookup(vnode_t **result, vnode_t *start, char *path, char *lastcomp, int flags);

static inline mode_t vfs_getposixtype(int type) {
	switch (type) {
		case V_TYPE_REGULAR:
			return TYPE_REGULAR;
		case V_TYPE_DIR:
			return TYPE_DIR;
		case V_TYPE_CHDEV:
			return TYPE_CHARDEV;
		case V_TYPE_BLKDEV:
			return TYPE_BLOCKDEV;
		case V_TYPE_FIFO:
			return TYPE_FIFO;
		case V_TYPE_LINK:
			return TYPE_LINK;
		case V_TYPE_SOCKET:
			return TYPE_SOCKET;
		default:
			return 0;
	}
}

static inline mode_t vfs_getsystemtype(int type) {
	switch (type) {
		case TYPE_REGULAR:
			return V_TYPE_REGULAR;
		case TYPE_DIR:
			return V_TYPE_DIR;
		case TYPE_CHARDEV:
			return V_TYPE_CHDEV;
		case TYPE_BLOCKDEV:
			return V_TYPE_BLKDEV;
		case TYPE_FIFO:
			return V_TYPE_FIFO;
		case TYPE_LINK:
			return V_TYPE_LINK;
		case TYPE_SOCKET:
			return V_TYPE_SOCKET;
		default:
			return -1;
	}
}

#endif
