#ifndef _MOUSE_H
#define _MOUSE_H

#include <ringbuffer.h>
#include <kernel/poll.h>
#include <mutex.h>

#define MOUSE_FLAG_RB 1
#define MOUSE_FLAG_MB 2
#define MOUSE_FLAG_LB 4
#define MOUSE_FLAG_B4 8
#define MOUSE_FLAG_B5 16

typedef struct {
	int flags;
	int x,y,z;
} mousepacket_t;

typedef struct {
	ringbuffer_t packetbuffer;
	spinlock_t lock;
	pollheader_t pollheader;
	int flags;
	mutex_t readmutex;
} mouse_t;

void mouse_init();
mouse_t *mouse_new();
void mouse_packet(mouse_t *mouse, mousepacket_t *packet);

#endif
