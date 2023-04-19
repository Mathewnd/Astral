#include <kernel/slab.h>
#include <kernel/vmm.h>
#include <logging.h>
#include <util.h>

#define SLAB_INDIRECT_CUTOFF 512
#define SLAB_INDIRECT_COUNT 16

#define SLAB_PAGE_OFFSET (PAGE_SIZE - sizeof(slab_t))
#define SLAB_DATA_SIZE SLAB_PAGE_OFFSET
#define SLAB_INDIRECT_PTR_COUNT (SLAB_DATA_SIZE / sizeof(void **))

#define GET_SLAB(x) (slab_t *)(ROUND_DOWN((uintptr_t)x, PAGE_SIZE) + SLAB_PAGE_OFFSET)

// the cache responsible for allocating all others
static scache_t selfcache = {
	.size = sizeof(scache_t),
	.alignment = 8,
	.truesize = ROUND_UP(sizeof(scache_t) + sizeof(void **), 8),
	.slabobjcount = SLAB_DATA_SIZE / ROUND_UP(sizeof(scache_t) + sizeof(void **), 8)
};

static void initdirect(scache_t *cache, slab_t *slab, void *base) {
	slab->free = NULL;
	slab->used = 0;

	for (uintmax_t offset = 0; offset < cache->truesize * cache->slabobjcount; offset += cache->truesize) {
		if (cache->ctor)
			cache->ctor(cache, (void *)((uintptr_t)base + offset));

		void **freenext = (void **)((uintptr_t)base + offset + cache->size);
		*freenext = slab->free;
		slab->free = freenext;
	}
}

static void initindirect(scache_t *cache, slab_t *slab, void **base, void *objbase) {
	slab->free = NULL;
	slab->used = 0;
	slab->base = objbase;

	for (uintmax_t offset = 0; offset < cache->truesize * cache->slabobjcount; offset += cache->truesize) {
		if (cache->ctor)
			cache->ctor(cache, (void *)((uintptr_t)objbase + offset));

		void **freenext = &base[offset / cache->truesize];
		*freenext =  slab->free;
		slab->free = freenext;
	}
}

static bool growcache(scache_t *cache) {
	void *_slab = vmm_map(NULL, PAGE_SIZE, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAG_READ | ARCH_MMU_FLAG_WRITE | ARCH_MMU_FLAG_NOEXEC, NULL);
	if (_slab == NULL)
		return false;
	slab_t *slab = GET_SLAB(_slab);

	if (cache->size < SLAB_INDIRECT_CUTOFF) {
		initdirect(cache, slab, _slab);
	} else {
		void *base = vmm_map(NULL, cache->slabobjcount * cache->truesize, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAG_READ | ARCH_MMU_FLAG_WRITE | ARCH_MMU_FLAG_NOEXEC, NULL);
		if (base == NULL) {
			vmm_unmap(_slab, PAGE_SIZE, 0);
			return false;
		}
		initindirect(cache, slab, _slab, base);
	}


	slab->next = cache->empty;
	cache->empty = slab;
	return true;
}

static void *takeobject(scache_t *cache, slab_t *slab) {
	if (slab->free == NULL)
		return NULL;

	void *objend = slab->free;
	slab->free = *slab->free;
	slab->used += 1;
	if (cache->size < SLAB_INDIRECT_CUTOFF)
		return (void *)((uintptr_t)objend - cache->size);
	else
		return (void *)((uintptr_t)slab->base + ((uintptr_t)objend - ROUND_DOWN((uintptr_t)slab, PAGE_SIZE)) / sizeof(void **) * cache->truesize);
}

void *slab_allocate(scache_t *cache) {
	// TODO lock
	slab_t *slab = NULL;
	if (cache->partial != NULL)
		slab = cache->partial;
	else if (cache->empty != NULL)
		slab = cache->empty;

	void *ret = NULL;

	if (slab == NULL) {
		if (growcache(cache))
			slab = cache->empty;
		else
			goto cleanup;
	}

	ret = takeobject(cache, slab);

	if (slab == cache->empty) {
		cache->empty = slab->next;
		slab->next = cache->partial;
		cache->partial = slab;
	}

	if (slab->used == cache->slabobjcount) {
		cache->partial = slab->next;
		slab->next = cache->full;
		cache->full = slab;
	}

	cleanup:
	// TODO unlock
	return ret;
}

scache_t *slab_newcache(size_t size, size_t alignment, void (*ctor)(scache_t *, void *), void (*dtor)(scache_t *, void *)) {
	if (alignment == 0)
		alignment = 8;

	scache_t *cache = slab_allocate(&selfcache);
	if (cache == NULL)
		return NULL;

	cache->size = size;
	cache->alignment = alignment;
	size_t freeptrsize = size < SLAB_INDIRECT_CUTOFF ? sizeof(void **) : 0;
	cache->truesize = ROUND_UP(size + freeptrsize, alignment);
	cache->ctor = ctor;
	cache->dtor = dtor;
	cache->slabobjcount = size < SLAB_INDIRECT_CUTOFF ? SLAB_DATA_SIZE / cache->truesize : SLAB_INDIRECT_COUNT;

	printf("slab: new cache: size %lu align %lu truesize %lu objcount %lu\n", cache->size, cache->alignment, cache->truesize, cache->slabobjcount);

	return cache;
}
