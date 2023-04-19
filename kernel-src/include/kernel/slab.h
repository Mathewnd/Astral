#ifndef _SLAB_H
#define _SLAB_H

#include <stddef.h>
#include <stdint.h>

typedef struct slab_t {
	struct slab_t *next;
	struct slab_t *prev;
	size_t used;
	void **free;
} slab_t;

typedef struct scache_t {
	// TODO lock
	void (*ctor)(struct scache_t *cache, void *obj);
	void (*dtor)(struct scache_t *cache, void *obj);
	slab_t *full;
	slab_t *partial;
	slab_t *empty;
	size_t size;
	size_t truesize;
	size_t alignment;
	size_t slabobjcount;
} scache_t;

void slab_init();

#endif
