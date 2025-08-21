#ifndef _BITMAP_H
#define _BITMAP_H

#include <stddef.h>

typedef struct {
	long *data;
	size_t size;
} bitmap_t;

int bitmap_init(bitmap_t *bitmap, size_t size);
void bitmap_set(bitmap_t *bitmap, long idx, int v);
long bitmap_find_first_set(bitmap_t *bitmap, long start_offset);


#endif
