#ifndef _IOVEC_H
#define _IOVEC_H

#include <stddef.h>
#include <stdbool.h>
#include <ringbuffer.h>

typedef struct {
	void *addr;
	size_t len;
} iovec_t;

typedef struct {
	iovec_t *iovec;
	size_t count;

	iovec_t *current;
	uintmax_t current_offset;

	uintmax_t total_offset;
	size_t total_size;
} iovec_iterator_t;

// checks if all iovec[x].addr is an userspace address
bool iovec_user_check(iovec_t *iovec, size_t count);

// returns the sum of the size of all iovecs
size_t iovec_size(iovec_t *iovec, size_t count);

// initializes an iovec iterator
void iovec_iterator_init(iovec_iterator_t *iovec_iterator, iovec_t *iovec, size_t count);

// skips skip_count bytes in the iovec iterator, returns the remaining number of bytes
size_t iovec_iterator_skip(iovec_iterator_t *iovec_iterator, size_t skip_count);

// sets the current offset of the iovec iterator to offset, returns the remaining number of bytes
size_t iovec_iterator_set(iovec_iterator_t *iovec_iterator, size_t offset);

// copies byte_count bytes from the iovec_iterator into buffer
// NOTE: the iovec can have userspace addresses. in this case, the copy can possibly fail with EFAULT
int iovec_iterator_copy_to_buffer(iovec_iterator_t *iovec_iterator, void *buffer, size_t byte_count);

// copies byte_count bytes from buffer into the iovec_iterator
// NOTE: the iovec can have userspace addresses. in this case, the copy can possibly fail with EFAULT
int iovec_iterator_copy_from_buffer(iovec_iterator_t *iovec_iterator, void *buffer, size_t byte_count);

// copies byte_count bytes from the iovec_iterator into the ringbuffer
// returns the number of bytes copied
// NOTE: the iovec can have userspace addresses. in this case, the copy can possibly fail by returning RINGBUFFER_USER_COPY_FAILED
size_t iovec_iterator_write_to_ringbuffer(iovec_iterator_t *iovec_iterator, ringbuffer_t *ringbuffer, size_t byte_count);

// copies byte_count bytes from the ringbuffer into the iovec_iterator
// returns the number of bytes copied
// NOTE: the iovec can have userspace addresses. in this case, the copy can possibly fail with RINGBUFFER_USER_COPY_FAILED
size_t iovec_iterator_read_from_ringbuffer(iovec_iterator_t *iovec_iterator, ringbuffer_t *ringbuffer, size_t byte_count);

// copies byte_count bytes from the ringbuffer into the iovec_iterator, without moving the ringbuffer read pointer
// returns the number of bytes copied
// NOTE: the iovec can have userspace addresses. in this case, the copy can possibly fail with RINGBUFFER_USER_COPY_FAILED
size_t iovec_iterator_peek_from_ringbuffer(iovec_iterator_t *iovec_iterator, ringbuffer_t *ringbuffer, size_t byte_count, uintmax_t ringbuffer_offset);

// returns the physical address of the next page in the iovec_iterator, adding a reference count while at it
// fails with EINVAL if the iterator does not currently point to the start of a page or there is not enough space for a page
// fails with EFAULT if the page is not mapped
int iovec_iterator_next_page(iovec_iterator_t *iovec_iterator, void **page);

#endif
