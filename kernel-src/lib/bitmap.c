#include <bitmap.h>
#include <errno.h>
#include <kernel/alloc.h>
#include <util.h>
#include <logging.h>

int bitmap_init(bitmap_t *bitmap, size_t size) {
	bitmap->data = alloc(size);
	bitmap->size = size;

	return bitmap->data ? 0 : ENOMEM;
}

void bitmap_set(bitmap_t *bitmap, long idx, int v) {
	__assert(bitmap->size > idx);

	long bit = 1llu << (idx % sizeof(*bitmap->data));

	if (v)
		bitmap->data[idx / sizeof(*bitmap->data)] |= bit;
	else
		bitmap->data[idx / sizeof(*bitmap->data)] &= ~bit;
}

// TODO fix start offset search
long bitmap_find_first_set(bitmap_t *bitmap, long start_offset) {
	size_t loop_size = ROUND_UP(bitmap->size, sizeof(*bitmap->data)) / sizeof(*bitmap->data);

	long offset = -1;

	for (size_t i = start_offset; i < loop_size; ++i) {
		if (bitmap->data[i] == 0)
			continue;

		offset = i * sizeof(*bitmap->data) + __builtin_ctz(bitmap->data[i]);
		break;
	}

	return offset;
}
