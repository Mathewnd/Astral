#ifndef _DEVFS_H
#define _DEVFS_H

#include <kernel/vfs.h>

typedef struct {
	int (*open)(int minor, int flags);
	int (*close)(int minor, int flags);
	int (*read)(int minor, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc);
	int (*write)(int minor, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec);
	int (*poll)(int minor, int events);
} devops_t;

typedef struct devnode_t {
	vnode_t vnode;
	vattr_t attr;
	devops_t *devops;
	vnode_t *physical;
	struct devnode_t *master;
} devnode_t;

void devfs_init();

#endif
