#include <stdint.h>

#include <u80211/kernel_interface.h>
#include <u80211/ringbuffer.h>
#include <u80211/status.h>
#include <u80211/string.h>
#include <u80211/util.h>

int u80211_ringbuffer_init(u80211_ringbuffer_t *ringbuffer, size_t size) {
	ringbuffer->size = 0;
	ringbuffer->write = 0;
	ringbuffer->read = 0;
	ringbuffer->data = NULL;

	if (size == 0)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	ringbuffer->data = u80211_kernel_allocate(size);
	if (ringbuffer->data == NULL)
		return U80211_STATUS_ENOMEM;

	ringbuffer->size = size;
	ringbuffer->write = 0;
	ringbuffer->read = 0;
	return U80211_STATUS_SUCCESS;
}

void u80211_ringbuffer_destroy(u80211_ringbuffer_t *ringbuffer) {
	if (ringbuffer->data != NULL)
		u80211_kernel_free(ringbuffer->data);
	ringbuffer->size = 0;
	ringbuffer->write = 0;
	ringbuffer->read = 0;
	ringbuffer->data = NULL;
}

size_t u80211_ringbuffer_truncate(u80211_ringbuffer_t *ringbuffer, size_t count) {
	size_t true_count = min(U80211_RINGBUFFER_DATA_COUNT(ringbuffer), count);
	ringbuffer->read += true_count;
	return true_count;
}

size_t u80211_ringbuffer_read(u80211_ringbuffer_t *ringbuffer, void *buffer, size_t count) {
	count = min(count, U80211_RINGBUFFER_DATA_COUNT(ringbuffer));
	size_t first_pass_offset = ringbuffer->read % ringbuffer->size;
	size_t first_pass_remaining = ringbuffer->size - first_pass_offset;
	size_t first_pass_count = min(count, first_pass_remaining);

	u80211_memcpy(buffer, (const void *)((uintptr_t)ringbuffer->data + first_pass_offset), first_pass_count);
	if (first_pass_count != count)
		u80211_memcpy((void *)((uintptr_t)buffer + first_pass_count), ringbuffer->data, count - first_pass_count);

	ringbuffer->read += count;
	return count;
}

size_t u80211_ringbuffer_peek(const u80211_ringbuffer_t *ringbuffer, void *buffer, uintmax_t offset, size_t count) {
	size_t data_count = U80211_RINGBUFFER_DATA_COUNT(ringbuffer);
	if (offset >= data_count)
		return 0;

	uintmax_t read = ringbuffer->read + offset;
	count = min(count, data_count - offset);
	size_t first_pass_offset = read % ringbuffer->size;
	size_t first_pass_remaining = ringbuffer->size - first_pass_offset;
	size_t first_pass_count = min(count, first_pass_remaining);

	u80211_memcpy(buffer, (const void *)((uintptr_t)ringbuffer->data + first_pass_offset), first_pass_count);
	if (first_pass_count != count)
		u80211_memcpy((void *)((uintptr_t)buffer + first_pass_count), ringbuffer->data, count - first_pass_count);

	return count;
}

size_t u80211_ringbuffer_write(u80211_ringbuffer_t *ringbuffer, const void *buffer, size_t count) {
	count = min(count, U80211_RINGBUFFER_FREE_SPACE(ringbuffer));
	size_t first_pass_offset = ringbuffer->write % ringbuffer->size;
	size_t first_pass_remaining = ringbuffer->size - first_pass_offset;
	size_t first_pass_count = min(count, first_pass_remaining);

	u80211_memcpy((void *)((uintptr_t)ringbuffer->data + first_pass_offset), buffer, first_pass_count);
	if (first_pass_count != count)
		u80211_memcpy(ringbuffer->data, (const void *)((uintptr_t)buffer + first_pass_count), count - first_pass_count);

	ringbuffer->write += count;
	return count;
}

size_t u80211_ringbuffer_remove(u80211_ringbuffer_t *ringbuffer, size_t count) {
	size_t true_count = min(U80211_RINGBUFFER_DATA_COUNT(ringbuffer), count);
	ringbuffer->write -= true_count;
	return true_count;
}
