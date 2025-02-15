#include <kernel/alloc.h>
#include <kernel/slab.h>
#include <logging.h>
#include <string.h>

// each allocation has the following structure:
// ptr: data capacity (allocsizes size)
// ptr + sizeof(size_t): current size
// ptr + sizeof(size_t) * 2: data
// ptr + datasize: poison value

#define USE_POISON 0
#define POISON_VALUE 0xdeadbeefbadc0ffel
#define CACHE_COUNT 12

#define CAPACITY_SIZE(cache) cache->size - sizeof(size_t) * 2 - USE_POISON * sizeof(size_t)

static size_t allocsizes[CACHE_COUNT] = {32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
static scache_t *caches[CACHE_COUNT + 5];

static void initarea(scache_t *cache, void *obj, size_t size) {
	size_t *ptr = obj;
	*ptr = CAPACITY_SIZE(cache);
	*(ptr + 1) = size;
	memset(ptr + 2, 0, *ptr);
	#if USE_POISON == 1
	*((size_t *)((uintptr_t)obj + cache->size - sizeof(size_t))) = POISON_VALUE;
	#endif
}

static scache_t *getcachefromsize(size_t size) {
	if (unlikely(size <= 1))
		return caches[0];

	size_t i = 64 - __builtin_clzll(size - 1);
	return caches[i];
}

void *alloc(size_t size) {
	scache_t *cache = getcachefromsize(size);
	size_t *ret = slab_allocate(cache);
	if (unlikely(ret == NULL))
		return NULL;

	initarea(cache, ret, size);
	#if USE_POISON == 1
		__assert(*ret == CAPACITY_SIZE(cache));
	#endif
	return ret + 2;
}

void free(void *ptr) {
	slab_free(getcachefromsize(*((size_t *)ptr - 2)), (size_t *)ptr - 2);
}

void *realloc(void *ptr, size_t size) {
	size_t *start = ptr;
	start -= 2;
	size_t currentsize = *(start + 1);
	if (size <= currentsize) {
		*(start + 1) = size;
		return ptr;
	}

	// grow
	scache_t *oldcache = getcachefromsize(*start);
	scache_t *newcache = getcachefromsize(size);

	// same allocation
	if (oldcache == newcache) {
		size_t diff = size - currentsize;
		memset((void *)((uintptr_t)ptr + currentsize), 0, diff);
		*(start + 1) = size;
		return ptr;
	}

	// different allocation
	size_t *new = slab_allocate(newcache);
	if (new == NULL)
		return NULL;
	initarea(newcache, new, size);
	memcpy((new + 2), ptr, currentsize);
	slab_free(oldcache, start);
	return new + 2;
}

void alloc_init() {
	for (int i = 0; i < CACHE_COUNT; ++i) {
		caches[i + 5] = slab_newcache(allocsizes[i] + sizeof(size_t) * 2 + sizeof(size_t) * USE_POISON, 0, NULL, NULL);
		__assert(caches[i + 5]);
		void *p = slab_allocate(caches[i + 5]);
		__assert(p);
		slab_free(caches[i + 5], p);
	}

	for (int i = 0; i < 5; ++i)
		caches[i] = caches[5];
}
