#ifndef _SLAB_H
#define _SLAB_H

#include <stddef.h>
#include <stdint.h>
#include <mutex.h>

typedef struct slab_t {
	struct slab_t *next;
	struct slab_t *prev;
	size_t used;
	void **free;
	void *base;
} slab_t;

typedef struct scache_t {
	mutex_t mutex;
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

void *slab_allocate(scache_t *cache);
void slab_free(scache_t *cache, void *addr);
scache_t *slab_newcache(size_t size, size_t alignment, void (*ctor)(scache_t *, void *), void (*dtor)(scache_t *, void *));
void slab_freecache(scache_t *cache);

#endif
