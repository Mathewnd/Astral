#ifndef _DEVFS_H
#define _DEVFS_H

#include <kernel/vfs.h>

#define DEV_MAJOR_NULL 1
#define DEV_MAJOR_FULL 2
#define DEV_MAJOR_ZERO 3
#define DEV_MAJOR_CONSOLE 4

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
int devfs_getnode(vnode_t *physical, int major, int minor, vnode_t **node);
int devfs_register(devops_t *devops, char *name, int type, int major, int minor, mode_t mode);

#endif
