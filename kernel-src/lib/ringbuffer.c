#include <ringbuffer.h>
#include <kernel/vmm.h>
#include <util.h>
#include <errno.h>
#include <string.h>
#include <kernel/usercopy.h>

int ringbuffer_init(ringbuffer_t *ringbuffer, size_t size) {
	ringbuffer->data = vmm_map(NULL, ROUND_UP(size, PAGE_SIZE), VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_NOEXEC, NULL);
	if (ringbuffer->data == NULL)
		return ENOMEM;

	ringbuffer->size = size;
	ringbuffer->write = 0;
	ringbuffer->read = 0;
	return 0;
}

void ringbuffer_destroy(ringbuffer_t *ringbuffer) {
	vmm_unmap(ringbuffer->data, ROUND_UP(ringbuffer->size, PAGE_SIZE), 0);
}

size_t ringbuffer_truncate(ringbuffer_t *ringbuffer, size_t count) {
	size_t truecount = min(RINGBUFFER_DATACOUNT(ringbuffer), count);
	ringbuffer->read += truecount;
	return truecount;
}

size_t ringbuffer_read(ringbuffer_t *ringbuffer, void *buffer, size_t count) {
	size_t datacount = RINGBUFFER_DATACOUNT(ringbuffer);
	count = min(count, datacount);
	size_t firstpassoffset = ringbuffer->read % ringbuffer->size;
	size_t firstpassremaining = ringbuffer->size - firstpassoffset;
	size_t firstpasscount = min(count, firstpassremaining);

	if (USERCOPY_POSSIBLY_FROM_USER(buffer, (void *)((uintptr_t)ringbuffer->data + firstpassoffset), firstpasscount))
		return RINGBUFFER_USER_COPY_FAILED;

	if (firstpasscount == count)
		goto leave;

	if (USERCOPY_POSSIBLY_FROM_USER((void *)((uintptr_t)buffer + firstpasscount), ringbuffer->data, count - firstpasscount))
		return RINGBUFFER_USER_COPY_FAILED;

	leave:
	ringbuffer->read += count;
	return count;
}

size_t ringbuffer_peek(ringbuffer_t *ringbuffer, void *buffer, uintmax_t offset, size_t count) {
	size_t datacount = RINGBUFFER_DATACOUNT(ringbuffer);
	size_t freespace = RINGBUFFER_SIZE(ringbuffer) - datacount;
	if (offset >= datacount)
		return 0;

	uintmax_t read = ringbuffer->read + min(offset, freespace);

	size_t offsetdatacount = datacount - offset;
	count = min(count, offsetdatacount);
	size_t firstpassoffset = read % ringbuffer->size;
	size_t firstpassremaining = ringbuffer->size - firstpassoffset;
	size_t firstpasscount = min(count, firstpassremaining);

	if (USERCOPY_POSSIBLY_TO_USER(buffer, (void *)((uintptr_t)ringbuffer->data + firstpassoffset), firstpasscount))
		return RINGBUFFER_USER_COPY_FAILED;

	if (firstpasscount == count)
		return count;

	if (USERCOPY_POSSIBLY_TO_USER((void *)((uintptr_t)buffer + firstpasscount), ringbuffer->data, count - firstpasscount))
		return RINGBUFFER_USER_COPY_FAILED;

	return count;
}

size_t ringbuffer_write(ringbuffer_t *ringbuffer, void *buffer, size_t count) {
	size_t freespace = RINGBUFFER_SIZE(ringbuffer) - RINGBUFFER_DATACOUNT(ringbuffer);
	count = min(count, freespace);
	size_t firstpassoffset = ringbuffer->write % ringbuffer->size;
	size_t firstpassremaining = ringbuffer->size - firstpassoffset;
	size_t firstpasscount = min(count, firstpassremaining);

	if (USERCOPY_POSSIBLY_FROM_USER((void *)((uintptr_t)ringbuffer->data + firstpassoffset), buffer, firstpasscount))
		return RINGBUFFER_USER_COPY_FAILED;

	if (firstpasscount == count)
		goto leave;

	if (USERCOPY_POSSIBLY_FROM_USER(ringbuffer->data, (void *)((uintptr_t)buffer + firstpasscount), count - firstpasscount))
		return RINGBUFFER_USER_COPY_FAILED;

	leave:
	ringbuffer->write += count;
	return count;
}

size_t ringbuffer_remove(ringbuffer_t *ringbuffer, size_t count) {
	size_t true_count = min(RINGBUFFER_DATACOUNT(ringbuffer), count);
	ringbuffer->write -= true_count;
	return true_count;
}
