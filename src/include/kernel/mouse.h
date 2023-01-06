#ifndef _MOUSE_H_INCLUDE
#define _MOUSE_H_INCLUDE

#define MOUSE_FLAG_RB 1
#define MOUSE_FLAG_MB 2
#define MOUSE_FLAG_LB 4
#define MOUSE_FLAG_B4 8
#define MOUSE_FLAG_B5 16

#include <ringbuffer.h>
#include <kernel/event.h>

typedef struct {
	int flags;
	int x,y,z;
} mousepacket_t;

typedef struct {
	ringbuffer_t buffer;
	event_t event;
	int flags;
} mouse_t;

void mouse_init();
int mouse_getnew();
void mouse_packet(int, mousepacket_t);

#endif
