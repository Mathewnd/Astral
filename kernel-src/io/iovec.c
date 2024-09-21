#include <kernel/iovec.h>
#include <kernel/vmm.h>
#include <kernel/usercopy.h>

static inline bool iovec_iterator_finished(iovec_iterator_t *iovec_iterator) {
	return iovec_iterator->total_size == iovec_iterator->total_offset;
}

bool iovec_user_check(iovec_t *iovec, size_t count) {
	for (int i = 0; i < count; ++i) {
		if (IS_USER_ADDRESS(iovec[i].addr) == false)
			return false;
	}

	return true;
}

size_t iovec_size(iovec_t *iovec, size_t count) {
	size_t size = 0;

	for (int i = 0; i < count; ++i)
		size += iovec[i].len;

	return size;
}

void iovec_iterator_init(iovec_iterator_t *iovec_iterator, iovec_t *iovec, size_t count) {
	iovec_iterator->iovec = iovec;
	iovec_iterator->count = count;

	iovec_iterator->current = iovec;
	iovec_iterator->current_offset = 0;

	iovec_iterator->total_offset = 0;
	iovec_iterator->total_size = iovec_size(iovec, count);
}

size_t iovec_iterator_skip(iovec_iterator_t *iovec_iterator, size_t skip_count) {
	for (;;) {
		size_t remaining_current = iovec_iterator->current->len - iovec_iterator->current_offset;
		size_t skip_current = min(remaining_current, skip_count);

		skip_count -= skip_current;

		iovec_iterator->total_offset += skip_current;
		iovec_iterator->current_offset += skip_current;

		// end of current iovec, go to next
		if (iovec_iterator->current_offset == iovec_iterator->current->len && iovec_iterator_finished(iovec_iterator) == false) {
			iovec_iterator->current++;
			iovec_iterator->current_offset = 0;
		}

		// end of skip or end of iovecs
		if (skip_count == 0 || iovec_iterator_finished(iovec_iterator))
			break;
	}

	return iovec_iterator->total_size - iovec_iterator->total_offset;
}

size_t iovec_iterator_set(iovec_iterator_t *iovec_iterator, size_t offset) {
	iovec_iterator->current = iovec_iterator->current;
	iovec_iterator->current_offset = 0;
	iovec_iterator->total_offset = 0;

	return iovec_iterator_skip(iovec_iterator, offset);
}

int iovec_iterator_copy_to_buffer(iovec_iterator_t *iovec_iterator, void *buffer, size_t byte_count) {
	size_t iterator_offset_save = iovec_iterator->total_offset;
	size_t remaining_total = min(byte_count, iovec_iterator->total_size - iovec_iterator->total_offset);
	size_t total_done = 0;
	int error = 0;

	for (;;) {
		size_t remaining_current = iovec_iterator->current->len - iovec_iterator->current_offset;
		size_t copy_current = min(remaining_current, remaining_total);

		error = USERCOPY_POSSIBLY_FROM_USER((void *)((uintptr_t)buffer + total_done), (void *)((uintptr_t)iovec_iterator->current->addr + iovec_iterator->current_offset), copy_current);
		if (error)
			break;

		iovec_iterator_skip(iovec_iterator, copy_current);
		total_done += copy_current;
		remaining_total -= copy_current;

		if (iovec_iterator_finished(iovec_iterator) || remaining_total == 0)
			break;
	}

	if (error)
		iovec_iterator_set(iovec_iterator, iterator_offset_save);

	return error;
}

int iovec_iterator_copy_from_buffer(iovec_iterator_t *iovec_iterator, void *buffer, size_t byte_count) {
	size_t iterator_offset_save = iovec_iterator->total_offset;
	size_t remaining_total = min(byte_count, iovec_iterator->total_size - iovec_iterator->total_offset);
	size_t total_done = 0;
	int error = 0;

	for (;;) {
		size_t remaining_current = iovec_iterator->current->len - iovec_iterator->current_offset;
		size_t copy_current = min(remaining_current, remaining_total);

		error = USERCOPY_POSSIBLY_FROM_USER((void *)((uintptr_t)iovec_iterator->current->addr + iovec_iterator->current_offset), (void *)((uintptr_t)buffer + total_done), copy_current);
		if (error)
			break;

		iovec_iterator_skip(iovec_iterator, copy_current);
		total_done += copy_current;
		remaining_total -= copy_current;

		if (iovec_iterator_finished(iovec_iterator) || remaining_total == 0)
			break;
	}

	if (error)
		iovec_iterator_set(iovec_iterator, iterator_offset_save);

	return error;
}

int iovec_iterator_memset(iovec_iterator_t *iovec_iterator, uint8_t byte, size_t byte_count) {
	size_t iterator_offset_save = iovec_iterator->total_offset;
	size_t remaining_total = min(byte_count, iovec_iterator->total_size - iovec_iterator->total_offset);
	int error = 0;

	for (;;) {
		size_t remaining_current = iovec_iterator->current->len - iovec_iterator->current_offset;
		size_t copy_current = min(remaining_current, remaining_total);

		error = USERCOPY_POSSIBLY_MEMSET_TO_USER((void *)((uintptr_t)iovec_iterator->current->addr + iovec_iterator->current_offset), byte, copy_current);
		if (error)
			break;

		iovec_iterator_skip(iovec_iterator, copy_current);
		remaining_total -= copy_current;

		if (iovec_iterator_finished(iovec_iterator) || remaining_total == 0)
			break;
	}

	if (error)
		iovec_iterator_set(iovec_iterator, iterator_offset_save);

	return error;
}

size_t iovec_iterator_write_to_ringbuffer(iovec_iterator_t *iovec_iterator, ringbuffer_t *ringbuffer, size_t byte_count) {
	size_t iterator_offset_save = iovec_iterator->total_offset;
	size_t remaining_total = min(byte_count, iovec_iterator->total_size - iovec_iterator->total_offset);
	size_t total_done = 0;
	size_t current_done = 0;

	for (;;) {
		size_t remaining_current = iovec_iterator->current->len - iovec_iterator->current_offset;
		size_t copy_current = min(remaining_current, remaining_total);

		current_done = ringbuffer_write(ringbuffer, (void *)((uintptr_t)iovec_iterator->current->addr + iovec_iterator->current_offset), copy_current);
		if (current_done == RINGBUFFER_USER_COPY_FAILED) {
			// remove the data that was inserted
			ringbuffer_remove(ringbuffer, total_done);
			break;
		}

		if (current_done == 0)
			break;

		iovec_iterator_skip(iovec_iterator, copy_current);
		remaining_total -= current_done;
		total_done += current_done;

		if (total_done == byte_count || iovec_iterator_finished(iovec_iterator))
			break;
	}

	if (current_done == RINGBUFFER_USER_COPY_FAILED) {
		iovec_iterator_set(iovec_iterator, iterator_offset_save);
		return RINGBUFFER_USER_COPY_FAILED;
	}

	return total_done;
}

size_t iovec_iterator_peek_from_ringbuffer(iovec_iterator_t *iovec_iterator, ringbuffer_t *ringbuffer, size_t offset, size_t byte_count) {
	size_t iterator_offset_save = iovec_iterator->total_offset;
	size_t remaining_total = min(byte_count, iovec_iterator->total_size - iovec_iterator->total_offset);
	size_t total_done = 0;
	size_t current_done = 0;

	for (;;) {
		size_t remaining_current = iovec_iterator->current->len - iovec_iterator->current_offset;
		size_t copy_current = min(remaining_current, remaining_total);

		current_done = ringbuffer_peek(ringbuffer, (void *)((uintptr_t)iovec_iterator->current->addr + iovec_iterator->current_offset), offset + total_done, copy_current);
		if (current_done == 0 || current_done == RINGBUFFER_USER_COPY_FAILED)
			break;

		iovec_iterator_skip(iovec_iterator, copy_current);
		total_done += current_done;
		remaining_total -= current_done;

		if (total_done == byte_count || iovec_iterator_finished(iovec_iterator))
			break;
	}

	if (current_done == RINGBUFFER_USER_COPY_FAILED) {
		iovec_iterator_set(iovec_iterator, iterator_offset_save);
		return RINGBUFFER_USER_COPY_FAILED;
	}

	return total_done;
}

size_t iovec_iterator_read_from_ringbuffer(iovec_iterator_t *iovec_iterator, ringbuffer_t *ringbuffer, size_t byte_count) {
	size_t done = iovec_iterator_peek_from_ringbuffer(iovec_iterator, ringbuffer, 0, byte_count);
	if (done == RINGBUFFER_USER_COPY_FAILED)
		return done;

	ringbuffer_truncate(ringbuffer, done);
	return done;
}

int iovec_iterator_next_page(iovec_iterator_t *iovec_iterator, void **page) {
	void *addr = (void *)((uintptr_t)iovec_iterator->current->addr + iovec_iterator->current_offset);
	if ((uintptr_t)addr % PAGE_SIZE)
		return EINVAL;

	size_t remaining = iovec_iterator->current->len - iovec_iterator->current_offset;
	if (remaining < PAGE_SIZE)
		return EINVAL;

	void *phys = vmm_getphysical(addr, true);
	if (phys == NULL)
		return EFAULT;

	*page = phys;
	return 0;
}
