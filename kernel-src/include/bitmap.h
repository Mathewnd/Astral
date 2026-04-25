#ifndef _BITMAP_H
#define _BITMAP_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
	long *data;
	size_t size;
} bitmap_t;

int bitmap_init(bitmap_t *bitmap, size_t size);
void bitmap_destroy(bitmap_t *bitmap);
void bitmap_set(bitmap_t *bitmap, long idx, int v);
void bitmap_set_range(bitmap_t *bitmap, long start, long count, int v);
bool bitmap_get(bitmap_t *bitmap, long idx);
long bitmap_find_first_set(bitmap_t *bitmap);

#endif
