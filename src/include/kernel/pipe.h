#ifndef _PIPE_H_INCLUDE
#define _PIPE_H_INCLUDE

#include <ringbuffer.h>
#include <kernel/event.h>
#include <stdint.h>
#include <stddef.h>

typedef struct{
	int lock;
	event_t revent, wevent;
	ringbuffer_t buff;
	uintmax_t readers, writers;
} pipe_t;

pipe_t* pipe_create(size_t buffsize);
void	pipe_drestroy(pipe_t* pipe);
int pipe_read(pipe_t* pipe, void* buff, size_t count, int* error);
int pipe_write(pipe_t* pipe, void* buff, size_t count, int* error);

#endif
