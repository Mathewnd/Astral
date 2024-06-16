#ifndef _TMPFS_H
#define _TMPFS_H

#include <kernel/vfs.h>
#include <hashtable.h>
#include <stdint.h>
#include <stddef.h>

typedef struct tmpfs_t {
	vfs_t vfs;
	uintmax_t inodenumber;
	uintmax_t id;
} tmpfs_t;

typedef struct tmpfsnode_t {
	vnode_t vnode;
	vattr_t attr;
	union {
		hashtable_t children;
		char *link;
	};
} tmpfsnode_t;

void tmpfs_init();

#endif
