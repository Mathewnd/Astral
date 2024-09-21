#ifndef _DEVFS_H
#define _DEVFS_H

#include <kernel/vfs.h>

#include <kernel/poll.h>
#include <hashtable.h>
#include <kernel/iovec.h>

#define DEV_MAJOR_NULL 1
#define DEV_MAJOR_FULL 2
#define DEV_MAJOR_ZERO 3
#define DEV_MAJOR_TTY 4
#define DEV_MAJOR_FB 5
#define DEV_MAJOR_KEYBOARD 6
#define DEV_MAJOR_BLOCK 7
#define DEV_MAJOR_E9 8
#define DEV_MAJOR_URANDOM 9
#define DEV_MAJOR_NET 10
#define DEV_MAJOR_MOUSE 11
#define DEV_MAJOR_PTY 12
#define DEV_MAJOR_ACPI 13

typedef struct {
	int (*open)(int minor, vnode_t **vnode, int flags);
	int (*close)(int minor, int flags);
	int (*read)(int minor, iovec_iterator_t *iovec_iterator, size_t size, uintmax_t offset, int flags, size_t *readc);
	int (*write)(int minor, iovec_iterator_t *iovec_iterator, size_t size, uintmax_t offset, int flags, size_t *writec);
	int (*poll)(int minor, polldata_t *data, int events);
	int (*mmap)(int minor, void *addr, uintmax_t offset, int flags);
	int (*munmap)(int minor, void *addr, uintmax_t offset, int flags);
	int (*isatty)(int minor);
	int (*ioctl)(int minor, unsigned long request, void *arg, int *result, cred_t *cred);
	int (*maxseek)(int minor, size_t *max);
	void (*inactive)(int minor);
} devops_t;

typedef struct devnode_t {
	vnode_t vnode;
	vattr_t attr;
	devops_t *devops;
	vnode_t *physical;
	struct devnode_t *master;
	hashtable_t children;
} devnode_t;

void devfs_init();
int devfs_getnode(vnode_t *physical, int major, int minor, vnode_t **node);
int devfs_register(devops_t *devops, char *name, int type, int major, int minor, mode_t mode, cred_t *cred);
int devfs_getbyname(char *name, vnode_t **ret);
int devfs_createdir(char *name);
void devfs_remove(char *name, int major, int minor);

#endif
