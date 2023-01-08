#ifndef _RINGBUFFER_H_INCLUDE
#define _RINGBUFFER_H_INCLUDE

#include <stdint.h>
#include <stddef.h>

typedef struct{
	int lock;
	size_t size;
	uintmax_t write, read;
	void* data;
} ringbuffer_t;


size_t ringbuffer_datacount(ringbuffer_t* ringubffer);
int ringbuffer_init(ringbuffer_t* ringbuffer, size_t size);
size_t ringbuffer_read(ringbuffer_t* ringbuffer, void* buffer, size_t count);
size_t ringbuffer_write(ringbuffer_t* ringbuffer, void* buffer, size_t count);


#endif
