#ifndef _EVENTFS_H
#define _EVENTFS_H

#include <kernel/vfs.h>

int eventfs_create_node(vnode_t **vnodep, size_t initval, bool semaphore);

#endif
