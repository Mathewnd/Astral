#ifndef _RINGBUFFER_H
#define _RINGBUFFER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	size_t size;
	uintmax_t write;
	uintmax_t read;
	void *data;
} ringbuffer_t;

int ringbuffer_init(ringbuffer_t *ringbuffer, size_t size);
void ringbuffer_destroy(ringbuffer_t *ringbuffer);
size_t ringbuffer_read(ringbuffer_t* ringbuffer, void *buffer, size_t count);
size_t ringbuffer_write(ringbuffer_t* ringbuffer, void *buffer, size_t count);

#define RINGBUFFER_DATACOUNT(x) ((x)->write - (x)->read)

#endif
