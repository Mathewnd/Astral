#ifndef _KEYBOARD_H_INCLUDE
#define _KEYBOARD_H_INCLUDE

#define KBPACKET_FLAGS_RELEASED 1

#include <ringbuffer.h>
#include <kernel/event.h>

typedef struct {
	ringbuffer_t buffer;
	event_t event;
	int flags;
} keyboard_t;

typedef struct {
	char ascii;
	uintmax_t keycode;
	int flags;
} kbpacket_t;

void keyboard_init();
int keyboard_getnew();


#endif
