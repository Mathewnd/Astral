#include <kernel/alloc.h>
#include <kernel/slab.h>
#include <logging.h>
#include <string.h>
#include <kernel/init.h>

#define DEBUG 1
#define POISON_VALUE 0xdeadbeefbadc0ffel
#define CACHE_COUNT 14

#if DEBUG == 1
typedef struct {
	size_t capacity;
	size_t current_size;
	size_t debug_address;
	uint8_t data[];
} alloc_header_t;
#else
typedef struct {
	size_t capacity;
	size_t current_size;
	uint8_t data[];
} alloc_header_t;
#endif

#define CAPACITY_SIZE(cache) cache->size - sizeof(alloc_header_t) - DEBUG * sizeof(size_t);

static size_t allocsizes[CACHE_COUNT] = {32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144};
static scache_t *caches[CACHE_COUNT + 5];

static void initarea(scache_t *cache, void *obj, size_t size) {
	alloc_header_t *hdr = obj;
	hdr->capacity = CAPACITY_SIZE(cache);
	hdr->current_size = size;
	memset(hdr->data, 0, hdr->capacity);
	#if DEBUG == 1
	*((size_t *)((uintptr_t)obj + cache->size - 1 * sizeof(size_t))) = POISON_VALUE;
	#endif
}

static scache_t *getcachefromsize(size_t size) {
	if (unlikely(size <= 1))
		return caches[0];

	if (unlikely(size > 262144))
		return NULL;

	size_t i = 64 - __builtin_clzll(size - 1);
	return caches[i];
}

void *alloc(size_t size) {
	scache_t *cache = getcachefromsize(size);
	if (unlikely(cache == NULL))
		return NULL;

	alloc_header_t *ret = slab_allocate(cache);
	if (unlikely(ret == NULL))
		return NULL;

	initarea(cache, ret, size);
	#if DEBUG == 1
		__assert(*(size_t *)((uintptr_t)ret + cache->size - sizeof(size_t)) == POISON_VALUE);
		ret->debug_address = (size_t)__builtin_return_address(0);
	#endif
	return ret->data;
}

void free(void *ptr) {
	alloc_header_t *header = container_of(ptr, alloc_header_t, data);
	scache_t *cache = getcachefromsize(header->capacity);
	#if DEBUG == 1
		__assert(cache);
		header->current_size = 0;
		__assert(*(size_t *)((uintptr_t)header + cache->size - sizeof(size_t)) == POISON_VALUE);
	#endif
	slab_free(cache, header);
}

void *realloc(void *ptr, size_t size) {
	alloc_header_t *header = container_of(ptr, alloc_header_t, data);
	if (size <= header->capacity) {
		header->current_size = size;
		return ptr;
	}

	// grow
	scache_t *old_cache = getcachefromsize(header->capacity);
	scache_t *new_cache = getcachefromsize(size);

	// same allocation
	if (old_cache == new_cache) {
		size_t diff = size - header->current_size;
		memset((void *)((uintptr_t)ptr + header->current_size), 0, diff);
		header->current_size = size;
		return ptr;
	}

	// different allocation
	alloc_header_t *new = slab_allocate(new_cache);
	if (new == NULL)
		return NULL;

	initarea(new_cache, new, size);
	memcpy(new->data, ptr, header->current_size);
	slab_free(old_cache, header);
	return new->data;
}

void alloc_init() {
	for (int i = 0; i < CACHE_COUNT; ++i) {
		caches[i + 5] = slab_newcache(allocsizes[i] + sizeof(alloc_header_t) + sizeof(size_t) * DEBUG, 0, NULL, NULL);
		__assert(caches[i + 5]);
		void *p = slab_allocate(caches[i + 5]);
		__assert(p);
		slab_free(caches[i + 5], p);
	}

	for (int i = 0; i < 5; ++i)
		caches[i] = caches[5];
}

INIT_ROUTINE_DEFINE(alloc, INIT_ROUTINE_FLAGS_NONE, alloc_init, slab);
