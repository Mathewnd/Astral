#ifndef _PIPEFS_H
#define _PIPEFS_H

#include <kernel/vfs.h>
#include <ringbuffer.h>
#include <semaphore.h>
#include <event.h>

typedef struct pipenode_t {
	vnode_t vnode;
	vattr_t attr;
	ringbuffer_t data;
	size_t readers, writers;
	event_t readevent, writeevent;
} pipenode_t;

void pipefs_init();
int pipefs_newpipe(vnode_t **nodep);

#endif
