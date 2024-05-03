#ifndef _PTY_H
#define _PTY_H

#include <kernel/tty.h>
#include <spinlock.h>
#include <ringbuffer.h>
#include <kernel/poll.h>

typedef struct {
	bool hangup;
	tty_t *tty;
	int minor;
	vnode_t *mastervnode;
	ringbuffer_t ringbuffer;
	pollheader_t pollheader;
	spinlock_t lock;
} pty_t;

void pty_init();

#endif
