#ifndef _PIPEFS_H
#define _PIPEFS_H

#include <kernel/vfs.h>
#include <ringbuffer.h>
#include <semaphore.h>
#include <kernel/poll.h>

typedef struct pipenode_t {
	vnode_t vnode;
	vattr_t attr;
	ringbuffer_t data;
	size_t readers, writers;
	pollheader_t pollheader;
} pipenode_t;

void pipefs_init();
int pipefs_newpipe(vnode_t **nodep);

#endif
