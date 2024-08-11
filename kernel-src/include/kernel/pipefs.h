#ifndef _PIPEFS_H
#define _PIPEFS_H

#include <kernel/vfs.h>
#include <ringbuffer.h>
#include <semaphore.h>
#include <kernel/poll.h>
#include <kernel/event.h>

typedef struct pipenode_t {
	vnode_t vnode;
	vattr_t attr;
	ringbuffer_t data;
	size_t readers, writers;
	pollheader_t pollheader;
	eventheader_t openevent;
} pipenode_t;

void pipefs_init();
int pipefs_newpipe(vnode_t **nodep);
int pipefs_getbinding(vnode_t *node, vnode_t **pipep);

#endif
