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

// initializes a ring buffer with a specific size
// returns an errno or 0 if successful
int ringbuffer_init(ringbuffer_t *ringbuffer, size_t size);

// destroys the ring buffer and frees the memory
void ringbuffer_destroy(ringbuffer_t *ringbuffer);

// reads count bytes from ring buffer into buffer
// returns -1 if it was unable to read (due to EFAULT when copying a user page)
// otherwise returns the number of bytes read
size_t ringbuffer_read(ringbuffer_t* ringbuffer, void *buffer, size_t count);

// writes count bytes from buffer into ring buffer
// returns -1 if it was unable to write (due to EFAULT when copying a user page)
// otherwise returns the number of bytes read
size_t ringbuffer_write(ringbuffer_t* ringbuffer, void *buffer, size_t count);

// removes count bytes from the read end (read goes forward)
// returns the number of bytes truncated
size_t ringbuffer_truncate(ringbuffer_t *ringbuffer, size_t count);

// same as ringbuffer_read, but accepts an offset and does not update the read pointer
// returns -1 if it was unable to read (due to EFAULT when copying a user page)
// otherwise returns the number of bytes read
size_t ringbuffer_peek(ringbuffer_t *ringbuffer, void *buffer, uintmax_t offset, size_t count);

// removes count bytes from the write end (write goes backwards)
// returns the number of bytes removed
size_t ringbuffer_remove(ringbuffer_t *ringbuffer, size_t count);

#define RINGBUFFER_DATACOUNT(x) ((x)->write - (x)->read)
#define RINGBUFFER_SIZE(x) (x)->size
#define RINGBUFFER_FREESPACE(x) (RINGBUFFER_SIZE(x) - RINGBUFFER_DATACOUNT(x))

#endif
