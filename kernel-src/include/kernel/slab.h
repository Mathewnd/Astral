#ifndef _SLAB_H
#define _SLAB_H

#include <stddef.h>
#include <stdint.h>
#include <mutex.h>
#include <hashtable.h>

typedef struct slab_t {
	struct slab_t *next;
	struct slab_t *prev;
	size_t reference_count;
	void **free;
	void *base;
} slab_t;

typedef struct magazine {
	struct magazine *next;
	size_t round_count;
	void *rounds[];
} magazine_t;

typedef struct {
	magazine_t *loaded_magazine;
	magazine_t *previous_magazine;
	mutex_t mutex;
} cache_per_cpu_t;

typedef struct slab_indirect {
	struct slab_indirect *next;
	slab_t *slab;
	void *object;
} slab_indirect_t;

typedef struct scache_t {
	mutex_t mutex;
	bool (*ctor)(struct scache_t *cache, void *obj);
	void (*dtor)(struct scache_t *cache, void *obj);
	slab_t *list;
	size_t size;
	size_t true_size;
	size_t alignment;
	size_t slab_object_count;
	slab_indirect_t **indirect_table;

	// magazine data
	size_t magazine_size;
	size_t magazine_minimum_size;
	size_t magazine_maximum_size;

	mutex_t depot_mutex;
	int contention_count;
	magazine_t *empty_depot;
	magazine_t *full_depot;

	cache_per_cpu_t per_cpu[];
} scache_t;

void *slab_allocate(scache_t *cache);
void slab_free(scache_t *cache, void *addr);
scache_t *slab_newcache(size_t size, size_t alignment, bool (*ctor)(scache_t *, void *), void (*dtor)(scache_t *, void *));
void slab_freecache(scache_t *cache);

#endif
