#include <kernel/alloc.h>
#include <kernel/slab.h>
#include <logging.h>
#include <string.h>

#define CACHE_COUNT 12

static size_t allocsizes[CACHE_COUNT] = {32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
static scache_t *caches[CACHE_COUNT];

static void initarea(scache_t *cache, void *obj) {
	size_t *ptr = obj;
	*ptr = cache->size - sizeof(size_t);
	memset(ptr + 1, 0, *ptr);
}

static scache_t *getcachefromsize(size_t size) {
	// XXX find a faster way to check this
	scache_t *cache = NULL;
	for (int i = 0; i < CACHE_COUNT; ++i) {
		if (size <= allocsizes[i]) {
			cache = caches[i];
			break;
		}
	}
	__assert(cache);
	return cache;
}

void *alloc(size_t size) {
	scache_t *cache = getcachefromsize(size);
	size_t *ret = slab_allocate(cache);
	if (ret == NULL)
		return NULL;
	return ret + 1;
}

void free(void *ptr) {
	size_t *start = ptr;
	start -= 1;
	size_t size = *start;
	scache_t *cache = getcachefromsize(size);
	slab_free(cache, start);
}

void alloc_init() {
	for (int i = 0; i < CACHE_COUNT; ++i) {
		caches[i] = slab_newcache(allocsizes[i] + sizeof(size_t), 0, initarea, initarea);
		__assert(caches[i]);
	}
}
