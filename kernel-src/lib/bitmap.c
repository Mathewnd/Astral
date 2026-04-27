#include <bitmap.h>
#include <errno.h>
#include <kernel/alloc.h>
#include <util.h>
#include <logging.h>

#define BYTE_INDEX(idx) ((idx) / (sizeof(long) * 8))
#define BIT_INDEX(idx) ((idx) % (sizeof(long) * 8))

int bitmap_init(bitmap_t *bitmap, size_t size) {
	bitmap->data = alloc(size);
	bitmap->size = size;

	if (bitmap->data != NULL)
		memset(bitmap->data, 0, size);

	return bitmap->data ? 0 : ENOMEM;
}

void bitmap_destroy(bitmap_t *bitmap) {
	free(bitmap->data);
	bitmap->data = NULL;
	bitmap->size = 0;
}

void bitmap_set(bitmap_t *bitmap, long idx, int v) {
	__assert(idx < bitmap->size);

	long *ptr = &bitmap->data[BYTE_INDEX(idx)];
	long bit = 1lu << BIT_INDEX(idx);

	if (v)
		*ptr |= bit;
	else
		*ptr &= ~bit;
}

void bitmap_set_range(bitmap_t *bitmap, long start, long count, int v) {
	__assert(start + count <= bitmap->size);

	for (long i = 0; i < count; ++i) {
		long idx = start + i;

		long *ptr = &bitmap->data[BYTE_INDEX(idx)];
		long bit = 1lu << BIT_INDEX(idx);

		if (v)
			*ptr |= bit;
		else
			*ptr &= ~bit;
	}
}

bool bitmap_get(bitmap_t *bitmap, long idx) {
	__assert(idx < bitmap->size);

	long bit = 1lu << BIT_INDEX(idx);
	return bitmap->data[BYTE_INDEX(idx)] & bit;
}

long bitmap_find_first_set(bitmap_t *bitmap) {
	size_t bits_per_long = sizeof(*bitmap->data) * 8;
	size_t loop_size = ROUND_UP(bitmap->size, bits_per_long) / bits_per_long;

	for (size_t i = 0; i < loop_size; ++i) {
		unsigned long word = bitmap->data[i];
		if (word == 0)
			continue;

		long offset = i * bits_per_long + __builtin_ctzl(word);
		if (offset < bitmap->size)
			return offset;
	}

	return -1;
}
