#ifndef _VFS_H
#define _VFS_h

#include <spinlock.h>
#include <stddef.h>
#include <stdint.h>

// TODO move typedefs to an abi header and use them here
typedef struct {
	int uid;
	int gid;
} cred_t;

typedef struct vfs_t {
	struct vfs_t *next;
	struct vfsops_t *ops;
	struct vnode_t *nodemountedon;
	int flags;
} vfs_t;

#define V_FLAG_ROOT 1

#define V_TYPE_REGULAR	0
#define V_TYPE_DIR	1
#define V_TYPE_CHDEV	2
#define V_TYPE_BLKDEV	3
#define V_TYPE_FIFO	4
#define V_TYPE_LINK	5
#define V_TYPE_SOCKET	6

#define V_FFLAG_READ 1
#define V_FFLAG_WRITE 2

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
	int (*read)(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, cred_t *cred);
	int (*write)(vnode_t *node, void *buffer, size_t size, uintmax_t offset, int flags, cred_t *cred);
	int (*lookup)(vnode_t *node, char *name, vnode_t **result, cred_t *cred);
	// TODO get/set/poll
} vops_t;

#define VFS_MOUNT(vfs, mp, b, d) (vfs)->ops->mount(vfs, mp, b, d)
#define VFS_UNMOUNT(vfs) (vfs)->ops->unmount(vfs)
#define VFS_ROOT(vfs, r) (vfs)->ops->root(vfs, r)
#define VFS_SYNC(vfs) (vfs)->ops->sync(vfs)

#define VOP_LOCK(v) spinlock_acquire(&(v)->lock)
#define VOP_UNLOCK(v) spinlock_release(&(v)->lock)

// these are expected to be called with the vnode locked
#define VOP_OPEN(v, f, c) (v)->ops->open(node, f, c)
#define VOP_CLOSE(v, f, c) (v)->ops->close(v, f, c)
#define VOP_READ(v, b, s, o, f, c) (v)->ops->read(v, b, s, o, f, c)
#define VOP_WRITE(v, b, s, o, f, c) (v)->ops->write(v, b, s, o, f, c)
#define VOP_LOOKUP(v, n, r, c) (v)->ops->lookup(v, n, r, c)
#define VOP_GETATTR(v, a, c) asdfmsadijsadsaddsadsadsadsadsad
#define VOP_SETATTR(v, a, c) sdfdskafiasdfoidsjfdsjofdsaopdosp

// lock not needed unless atomicity is desired
#define VOP_POLL(v, p, c) sdfmdsaofidasfdsafdsafdsaofodsajfodis
#define VOP_HOLD(v) __atomic_add_fetch(&v->refcount, 1, __ATOMIC_SEQ_CST)
#define VOP_RELEASE(v) __attomic_sub_fetch(&v->refcount, 1, __ATOMIC_SEQ_CST);

extern vnode_t *vfsroot;

void vfs_init();
int vfs_mount(vnode_t *mounton, char *name, void *data);
int vfs_register(vfsops_t *vfsops, char *name);

#endif
