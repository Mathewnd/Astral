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
	void *_slab = vmm_map(NULL, PAGE_SIZE, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	if (_slab == NULL)
		return false;
	slab_t *slab = GET_SLAB(_slab);

	if (cache->size < SLAB_INDIRECT_CUTOFF) {
		initdirect(cache, slab, _slab);
	} else {
		void *base = vmm_map(NULL, cache->slabobjcount * cache->truesize, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
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

	void **objend = slab->free;
	slab->free = *slab->free;
	slab->used += 1;
	*objend = NULL;
	if (cache->size < SLAB_INDIRECT_CUTOFF)
		return (void *)((uintptr_t)objend - cache->size);
	else
		return (void *)((uintptr_t)slab->base + ((uintptr_t)objend - ROUND_DOWN((uintptr_t)slab, PAGE_SIZE)) / sizeof(void **) * cache->truesize);
}

// frees an object and returns the slab the object belongs to
static slab_t *returnobject(scache_t *cache, void *obj) {
	slab_t *slab = NULL;
	void **freeptr = NULL;
	if (cache->size < SLAB_INDIRECT_CUTOFF) {
		slab = (slab_t *)(ROUND_DOWN((uintptr_t)obj, PAGE_SIZE) + SLAB_PAGE_OFFSET);
		freeptr = (void **)((uintptr_t)obj + cache->size);
		__assert(*freeptr == NULL);
	} else {
		slab = cache->full;
		bool partial = false;
		while (slab) {
			void *top = (void *)((uintptr_t)slab->base + cache->slabobjcount * cache->truesize);
			if (obj >= slab->base && obj < top)
				break;
			if (slab->next == NULL && partial == false) {
				slab = cache->partial;
				partial = true;
			} else {
				slab = slab->next;
			}
		}
		__assert(slab);
		uintmax_t objn = ((uintptr_t)obj - (uintptr_t)slab->base) / cache->truesize;
		void **base = (void **)ROUND_DOWN((uintptr_t)obj, PAGE_SIZE);
		freeptr = &base[objn];
	}

	if (cache->dtor)
		cache->dtor(cache, obj);

	*freeptr = slab->free;
	slab->free = freeptr;
	--slab->used;

	return slab;
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
		if (slab->prev)
			slab->prev->next = slab->next;
		if (slab->next)
			slab->next->prev = slab->prev;
		if (cache->partial)
			cache->partial->prev = slab;
		slab->next = cache->partial;
		slab->prev = NULL;
		cache->partial = slab;
	}

	if (slab->used == cache->slabobjcount) {
		cache->partial = slab->next;
		if (slab->prev)
			slab->prev->next = slab->next;
		if (slab->next)
			slab->next->prev = slab->prev;
		if (cache->full)
			cache->full->prev = slab;
		slab->next = cache->full;
		slab->prev = NULL;
		cache->full = slab;
	}

	cleanup:
	// TODO unlock
	return ret;
}

void slab_free(scache_t *cache, void *addr) {
	// TODO lock

	slab_t *slab = returnobject(cache, addr);
	__assert(slab);

	if (slab->used == 0) {
		cache->partial = slab->next;
		if (slab->prev)
			slab->prev->next = slab->next;
		if (slab->next)
			slab->next->prev = slab->prev;
		if (cache->empty)
			cache->empty->prev = slab;
		slab->next = cache->empty;
		slab->prev = NULL;
		cache->empty = slab;
	}
	
	if (slab->used == cache->slabobjcount - 1) {
		cache->full = slab->next;
		if (slab->prev)
			slab->prev->next = slab->next;
		if (slab->next)
			slab->next->prev = slab->prev;
		if (cache->partial)
			cache->partial->prev = slab;
		slab->next = cache->partial;
		slab->prev = NULL;
		cache->partial = slab;
	}

	// TODO unlock
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
	cache->full = NULL;
	cache->empty = NULL;
	cache->partial = NULL;

	printf("slab: new cache: size %lu align %lu truesize %lu objcount %lu\n", cache->size, cache->alignment, cache->truesize, cache->slabobjcount);

	return cache;
}

static size_t purge(scache_t *cache, size_t maxcount){
	slab_t *slab = cache->empty;
	for (size_t done = 0; done < maxcount; ++done) {
		if (slab == NULL)
			return done;

		slab_t *next = slab->next;
		if (next)
			next->prev = NULL;

		if (cache->size >= SLAB_INDIRECT_CUTOFF)
			vmm_unmap(slab->base, cache->slabobjcount * cache->truesize, 0);

		vmm_unmap(slab, PAGE_SIZE, 0);

		slab = cache->empty;
		cache->empty = next;
	}

	return maxcount;
}

void slab_freecache(scache_t *cache) {
	// TODO lock
	__assert(cache->partial == NULL);
	__assert(cache->full == NULL);

	purge(cache, (size_t)-1);

	slab_free(&selfcache, cache);

	// TODO unlock
}
