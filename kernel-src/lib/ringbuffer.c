#include <ringbuffer.h>
#include <kernel/vmm.h>
#include <util.h>
#include <errno.h>

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
	uint8_t *ptr = buffer;
	size_t readc = 0;
	for (; readc < count; ++readc) {
		if (ringbuffer->read == ringbuffer->write)
			break;

		uintmax_t readoffset = ringbuffer->read % ringbuffer->size;
		*ptr++ = *((uint8_t *)ringbuffer->data + readoffset);
		++ringbuffer->read;
	}
	return readc;
}

size_t ringbuffer_peek(ringbuffer_t *ringbuffer, void *buffer, uintmax_t offset, size_t count) {
	uint8_t *ptr = buffer;

	uintmax_t read = ringbuffer->read;
	uintmax_t write = ringbuffer->write;

	uintmax_t curroff = 0;
	for (; curroff < offset; ++curroff) {
		if (read == write)
			return 0;

		++read;
	}

	size_t readc = 0;
	for (; readc < count; ++readc) {
		if (read == write)
			break;

		uintmax_t readoffset = read % ringbuffer->size;
		*ptr++ = *((uint8_t *)ringbuffer->data + readoffset);
		++read;
	}

	return readc;
}

size_t ringbuffer_write(ringbuffer_t *ringbuffer, void *buffer, size_t count) {
	uint8_t *ptr = buffer;
	size_t writec = 0;
	for (;writec < count; ++writec) {
		if (ringbuffer->write == ringbuffer->read + ringbuffer->size)
			break;

		uintmax_t writeoffset = ringbuffer->write % ringbuffer->size;
		*((uint8_t *)ringbuffer->data + writeoffset) = *ptr++;
		++ringbuffer->write;
	}
	return writec;
}
