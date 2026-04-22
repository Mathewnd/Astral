#include <kernel/slab.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <logging.h>
#include <util.h>
#include <arch/smp.h>
#include <kernel/init.h>
#include <kernel/alloc.h>

#define SLAB_INDIRECT_CUTOFF 512
#define SLAB_INDIRECT_COUNT 16

#define SLAB_PAGE_OFFSET (PAGE_SIZE - sizeof(slab_t))
#define SLAB_DATA_SIZE SLAB_PAGE_OFFSET
#define SLAB_INDIRECT_PTR_COUNT (SLAB_DATA_SIZE / sizeof(void **))

#define GET_SLAB(x) (slab_t *)(ROUND_DOWN((uintptr_t)x, PAGE_SIZE) + SLAB_PAGE_OFFSET)

#define SLAB_DEBUG 0

static scache_t *self_cache;
static scache_t *magazine_cache;
static scache_t *slab_cache;
static scache_t *indirect_cache;
static scache_t *indirect_table_cache;

static inline bool grow_cache(scache_t *cache) {
	slab_t *slab;

	if (cache->size < SLAB_INDIRECT_CUTOFF) {
		void *slab_hhdm = pmm_allocpage(PMM_SECTION_DEFAULT);
		if (unlikely(slab_hhdm == NULL))
			return false;

		slab_hhdm = MAKE_HHDM(slab_hhdm);
		slab = GET_SLAB(slab_hhdm);
		slab->free = NULL;
		
		for (uintmax_t offset = 0; offset < cache->true_size * cache->slab_object_count; offset += cache->true_size) {
			void **freenext = (void **)((uintptr_t)slab_hhdm + offset + cache->size);
			*freenext = slab->free;
			slab->free = freenext;
		}
	} else {
		slab = slab_allocate(slab_cache);
		if (unlikely(slab == NULL))
			return false;

		size_t byte_size = cache->slab_object_count * cache->true_size;
		slab->base = vmm_map(NULL, byte_size, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
		if (unlikely(slab->base == NULL)) {
			slab_free(slab_cache, slab);
			return false;
		}

		slab_indirect_t *indirect[SLAB_INDIRECT_COUNT];
		int i;
		for (i = 0; i < SLAB_INDIRECT_COUNT; ++i) {
			indirect[i] = slab_allocate(indirect_cache);
			if (indirect[i] == NULL)
				break;

			indirect[i]->next = i == 0 ? NULL : indirect[i - 1];
			indirect[i]->slab = slab;
		}

		if (unlikely(i < SLAB_INDIRECT_COUNT)) {
			for (int j = 0; j < i; ++j)
				slab_free(indirect_cache, indirect[j]);
			
			vmm_unmap(slab->base, byte_size, 0);

			slab_free(slab_cache, slab);
			return false;
		}

		i = 0;
		for (uintmax_t offset = 0; offset < byte_size; offset += cache->true_size) {
			void *object = (void *)((uintptr_t)slab->base + offset);
			indirect[i++]->object = object;
		}

		slab->free = (void **)indirect[SLAB_INDIRECT_COUNT - 1];
	}

	slab->reference_count = 0;

	// it can be inserted directly into cache->list because if this is called there is nothing to allocate
	slab_t *list = cache->list;
	if (likely(cache->list)) {
		slab->next = list;
		slab->prev = list->prev;
		list->prev->next = slab;
		list->prev = slab;
	} else {
		slab->next = slab;
		slab->prev = slab;
	}

	cache->list = slab;

	return true;
}

static inline int get_index(void *p) {
	uintptr_t key = (uintptr_t)p;
	return fnv1ahash(&key, sizeof(key)) % 32;
}

static void *allocate_from_slab(scache_t *cache) {
	MUTEX_ACQUIRE(&cache->mutex);
	// if there is no slab or only an empty one, grow the cache
	if ((cache->list == NULL || cache->list->reference_count == cache->slab_object_count) && unlikely(grow_cache(cache) == false))
		return NULL;

	slab_t *slab = cache->list;

#if SLAB_DEBUG != 0
	__assert(slab != NULL && slab->reference_count != cache->slab_object_count);
#endif

	// take an object from the cache
	void **free_list = slab->free;
	slab->free = *free_list;
	++slab->reference_count;

#if SLAB_DEBUG != 0
	if (cache->size < SLAB_INDIRECT_CUTOFF) {
		void *addr = (void *)objend;
		void *base = (void *)ROUND_DOWN((uintptr_t)slab, PAGE_SIZE);
		__assert(addr >= base && addr < (void *)slab);
	} else {
		void *addr = slab->base + ((uintptr_t)objend - ROUND_DOWN((uintptr_t)slab, PAGE_SIZE)) / sizeof(void **) * cache->true_size;
		__assert(addr >= slab->base && (uintptr_t)addr < (uintptr_t)slab->base + cache->slab_object_count * cache->true_size);
	}
#endif

	void *ret;
	if (cache->size < SLAB_INDIRECT_CUTOFF) {
		ret = (void *)((uintptr_t)free_list - cache->size);
		if (cache->ctor && cache->ctor(cache, ret) == false) {
			slab->free = free_list;
			return NULL;
		}

#if SLAB_DEBUG != 0
		*free_list = NULL;
#endif
	} else {
		slab_indirect_t *indirect = (slab_indirect_t *)free_list;
		if (cache->ctor && cache->ctor(cache, indirect->object) == false) {
			slab->free = free_list;
			return NULL;
		}
		// add to hash table as well
		int idx = get_index(indirect->object);
		indirect->next = cache->indirect_table[idx];
		cache->indirect_table[idx] = indirect;

		ret = indirect->object;
	}

	// if the slab is empty, advance the linked list by one to the next partial or full slab
	if (slab->reference_count == cache->slab_object_count)
		cache->list = slab->next;

	MUTEX_RELEASE(&cache->mutex);
	return ret;
}

static void free_to_slab(scache_t *cache, void *addr) {
	MUTEX_ACQUIRE(&cache->mutex);

	// insert into a slab
	slab_t *slab = NULL;
	void **free_list = NULL;

	if (cache->size < SLAB_INDIRECT_CUTOFF) {
		slab = GET_SLAB(addr);
		free_list = (void **)((uintptr_t)addr + cache->size);

#if SLAB_DEBUG != 0
		__assert(*free_list == NULL);
#endif
	} else {
		int idx = get_index(addr);
		slab_indirect_t *prev = NULL;
		slab_indirect_t *indirect = cache->indirect_table[idx];

		while (indirect->object != addr) {
			prev = indirect;
			indirect = indirect->next;
		}

		if (prev)
			prev->next = indirect->next;
		else
			cache->indirect_table[idx] = indirect->next;

		slab = indirect->slab;
		free_list = (void **)indirect;

#if SLAB_DEBUG != 0
		__assert(slab);
		__assert(obj >= slab->base && addr < top);
#endif
	}

	if (cache->dtor)
		cache->dtor(cache, addr);

	*free_list = slab->free;
	slab->free = free_list;
	--slab->reference_count;

	// reorder lists if nescessary
	if (slab->reference_count == cache->slab_object_count - 1) {
		// if it became a partial slab, reorder the list
		slab->prev->next = slab->next;
		slab->next->prev = slab->prev;

		slab->next = cache->list;
		slab->prev = cache->list->prev;
		slab->prev->next = slab;
		slab->next->prev = slab;

		cache->list = slab;
	} else if (slab->reference_count == 0) {
		// if it became a full slab, reorder the list
		slab_t *search = cache->list;
		do {
			search = search->next;
		} while (search != cache->list && search->reference_count != cache->slab_object_count && (search->reference_count != 0 || search == slab));

		// if the list points to this slab and the next one is a partial, set the list to it
		// otherwise, the next is full (refcount 0) or empty (refcount == slab_object_count), which means this slab will be still be the next one to
		// be allocated out of
		if (cache->list == slab && slab->next->reference_count != 0 && slab->next->reference_count != cache->slab_object_count)
		       cache->list = slab->next;

		slab->prev->next = slab->next;
		slab->next->prev = slab->prev;

		slab->next = search;
		slab->prev = search->prev;
		search->prev = slab;
		slab->prev->next = slab;
	}

	MUTEX_RELEASE(&cache->mutex);
}
static inline void acquire_depot_mutex(scache_t *cache) {
	// keep track of contention for dynamic resizing of magazines in this cache
	if (MUTEX_TRY(&cache->depot_mutex)) {
		if (__atomic_fetch_sub(&cache->contention_count, 1, __ATOMIC_SEQ_CST) == 0)
			cache->contention_count = 0;

	} else {
		__atomic_add_fetch(&cache->contention_count, 1, __ATOMIC_SEQ_CST);
		MUTEX_ACQUIRE(&cache->depot_mutex);
	}
}

void *slab_allocate(scache_t *cache) {
	cache_per_cpu_t *cpu_cache = &cache->per_cpu[current_cpu_internal_id()];

	MUTEX_ACQUIRE(&cpu_cache->mutex);
	// are there any rounds in the loaded magazine?
	if (cpu_cache->loaded_magazine && cpu_cache->loaded_magazine->round_count) {
		void *ret = cpu_cache->loaded_magazine->rounds[--cpu_cache->loaded_magazine->round_count];
		MUTEX_RELEASE(&cpu_cache->mutex);
		return ret;
	}

	// are there any rounds in the previous magazine?
	if (cpu_cache->previous_magazine && cpu_cache->previous_magazine->round_count) {
		// swap loaded and previous
		magazine_t *swap = cpu_cache->loaded_magazine;
		cpu_cache->loaded_magazine = cpu_cache->previous_magazine;
		cpu_cache->previous_magazine = swap;

		void *ret = cpu_cache->loaded_magazine->rounds[--cpu_cache->loaded_magazine->round_count];
		MUTEX_RELEASE(&cpu_cache->mutex);
		return ret;
	}

	// check the depot for full magazines
	acquire_depot_mutex(cache);

	if (cache->full_depot) {
		// put the previous magazine into the depot
		if (cpu_cache->previous_magazine) {
			cpu_cache->previous_magazine->next = cache->empty_depot;
			cache->empty_depot = cpu_cache->previous_magazine;
		}

		// load a new full magazine
		cpu_cache->previous_magazine = cpu_cache->loaded_magazine;
		cpu_cache->loaded_magazine = cache->full_depot;
		cache->full_depot = cache->full_depot->next;

		void *ret = cpu_cache->loaded_magazine->rounds[--cpu_cache->loaded_magazine->round_count];
		MUTEX_RELEASE(&cache->depot_mutex);
		MUTEX_RELEASE(&cpu_cache->mutex);
		return ret;
	}

	MUTEX_RELEASE(&cache->depot_mutex);

	// fall back into allocating from the slab directly
	MUTEX_RELEASE(&cpu_cache->mutex);
	return allocate_from_slab(cache);
}

static magazine_t *allocate_magazine_for_cache(scache_t *cache) {
	return slab_allocate(magazine_cache);
}

void slab_free(scache_t *cache, void *addr) {
	cache_per_cpu_t *cpu_cache = &cache->per_cpu[current_cpu_internal_id()];

	MUTEX_ACQUIRE(&cpu_cache->mutex);
	// can insert into the loaded magazine?
	if (cpu_cache->loaded_magazine && cpu_cache->loaded_magazine->round_count < cache->magazine_size) {
		cpu_cache->loaded_magazine->rounds[cpu_cache->loaded_magazine->round_count++] = addr;
		MUTEX_RELEASE(&cpu_cache->mutex);
		return;
	}

	// can insert into previous magazine?
	// the check for 0 is valid as the previous magazines will either be full or empty
	if (cpu_cache->previous_magazine && cpu_cache->previous_magazine->round_count == 0) {
		// swap loaded and previous
		magazine_t *swap = cpu_cache->loaded_magazine;
		cpu_cache->loaded_magazine = cpu_cache->previous_magazine;
		cpu_cache->previous_magazine = swap;

		cpu_cache->loaded_magazine->rounds[cpu_cache->loaded_magazine->round_count++] = addr;
		MUTEX_RELEASE(&cpu_cache->mutex);
		return;
	}

	// can get an empty magazine from the depot?
	acquire_depot_mutex(cache);
	if (cache->empty_depot) {
		// put the previous magazine into the depot
		if (cpu_cache->previous_magazine) {
			cpu_cache->previous_magazine->next = cache->full_depot;
			cache->full_depot = cpu_cache->previous_magazine;
		}

		// load a new empty magazine
		cpu_cache->previous_magazine = cpu_cache->loaded_magazine;
		cpu_cache->loaded_magazine = cache->empty_depot;
		cache->empty_depot = cache->empty_depot->next;

		cpu_cache->loaded_magazine->rounds[cpu_cache->loaded_magazine->round_count++] = addr;
		MUTEX_RELEASE(&cache->depot_mutex);
		MUTEX_RELEASE(&cpu_cache->mutex);
		return;
	}

	// allocate an empty magazine
	magazine_t *new_magazine = allocate_magazine_for_cache(cache);
	if (new_magazine) {
		// put previous into the depot
		if (cpu_cache->previous_magazine) {
			cpu_cache->previous_magazine->next = cache->full_depot;
			cache->full_depot = cpu_cache->previous_magazine;
		}

		// load the new magazine
		cpu_cache->previous_magazine = cpu_cache->loaded_magazine;
		cpu_cache->loaded_magazine = new_magazine;

		cpu_cache->loaded_magazine->rounds[cpu_cache->loaded_magazine->round_count++] = addr;
		MUTEX_RELEASE(&cache->depot_mutex);
		MUTEX_RELEASE(&cpu_cache->mutex);
		return;
	}
	MUTEX_RELEASE(&cache->depot_mutex);

	// fall back into freeing to the slab directly
	MUTEX_RELEASE(&cpu_cache->mutex);
	return free_to_slab(cache, addr);
}

#define MAGAZINE_SIZE 8

static bool slab_initialize(scache_t *cache, size_t size, size_t alignment, bool (*ctor)(scache_t *, void *), void (*dtor)(scache_t *, void *)) {
	if (alignment == 0)
		alignment = 8;

	if (size >= SLAB_INDIRECT_CUTOFF) {
		cache->indirect_table = slab_allocate(indirect_table_cache);
		if (cache->indirect_table == NULL)
			return false;
		memset(cache->indirect_table, 0, sizeof(slab_indirect_t *) * 32);
	}

	cache->size = size;
	cache->alignment = alignment;
	size_t freeptrsize = size < SLAB_INDIRECT_CUTOFF ? sizeof(void **) : 0;
	cache->true_size = ROUND_UP(size + freeptrsize, alignment);
	cache->ctor = ctor;
	cache->dtor = dtor;
	cache->slab_object_count = size < SLAB_INDIRECT_CUTOFF ? SLAB_DATA_SIZE / cache->true_size : SLAB_INDIRECT_COUNT;
	cache->list = NULL;
	cache->magazine_size = MAGAZINE_SIZE;
	cache->magazine_minimum_size = MAGAZINE_SIZE;
	cache->magazine_maximum_size = MAGAZINE_SIZE;
	cache->contention_count = 0;
	cache->empty_depot = NULL;
	cache->full_depot = NULL;

	MUTEX_INIT(&cache->mutex);
	MUTEX_INIT(&cache->depot_mutex);

	for (int i = 0; i < arch_smp_get_cpu_count(); ++i) {
		cache_per_cpu_t *cpu_cache = &cache->per_cpu[i];
		cpu_cache->loaded_magazine = NULL;
		cpu_cache->previous_magazine = NULL;
		MUTEX_INIT(&cpu_cache->mutex);
	}

	return true;
}

scache_t *slab_newcache(size_t size, size_t alignment, bool (*ctor)(scache_t *, void *), void (*dtor)(scache_t *, void *)) {
	scache_t *cache = slab_allocate(self_cache);
	if (cache == NULL)
		return NULL;

	if (!slab_initialize(cache, size, alignment, ctor, dtor)) {
		slab_free(self_cache, cache);
		return NULL;
	}

	printf("slab: new cache: size %lu align %lu true_size %lu objcount %lu\n", cache->size, cache->alignment, cache->true_size, cache->slab_object_count);

	return cache;
}


static bool magazine_ctor(scache_t *, void *obj) {
	magazine_t *mag = obj;
	mag->next = NULL;
	mag->round_count = 0;

#if SLAB_DEBUG != 0
	memset(mag->rounds, 0xfe, MAGAZINE_SIZE * sizeof(void *));
#endif

	return true;
}

static scache_t *create_new_from_vmm(size_t size, size_t alignment, bool (*ctor)(scache_t *, void *), void (*dtor)(scache_t *, void *)) {
	size_t cache_size = sizeof(scache_t) + sizeof(cache_per_cpu_t) * arch_smp_get_cpu_count();
	scache_t *cache = vmm_map(NULL, cache_size, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	__assert(cache);
	__assert(slab_initialize(cache, size, alignment, ctor, dtor));
	return cache;
}

scache_t *slab_create_new_cache_from_pmm(size_t size, size_t alignment, bool (*ctor)(scache_t *, void *), void (*dtor)(scache_t *, void *)) {
	__assert(size < SLAB_INDIRECT_CUTOFF);
	size_t cache_size = sizeof(scache_t) + sizeof(cache_per_cpu_t) * arch_smp_get_cpu_count();
	scache_t *cache = pmm_alloc(ROUND_UP(cache_size, PAGE_SIZE) / PAGE_SIZE, PMM_SECTION_DEFAULT);
	__assert(cache);
	cache = MAKE_HHDM(cache);
	__assert(slab_initialize(cache, size, alignment, ctor, dtor));
	return cache;
}

// initializes enough to allow for the vmm to bootstrap
void slab_early_init(void) {
	size_t magazine_object_size = sizeof(magazine_t) + sizeof(void *) * MAGAZINE_SIZE;
	__assert(magazine_object_size < SLAB_INDIRECT_CUTOFF);
	magazine_cache = slab_create_new_cache_from_pmm(magazine_object_size, 0, magazine_ctor, NULL);
}

// TODO dynamically change the size of magazines
void slab_init(void) {
	size_t cache_size = sizeof(scache_t) + sizeof(cache_per_cpu_t) * arch_smp_get_cpu_count();
	// the cache of caches needs for the slab and indirect caches to be up
	slab_cache = create_new_from_vmm(sizeof(slab_t), 0, NULL, NULL);
	indirect_cache = create_new_from_vmm(sizeof(slab_indirect_t), 0, NULL, NULL);
	indirect_table_cache = create_new_from_vmm(sizeof(slab_indirect_t *) * 32, 0, NULL, NULL);
	self_cache = create_new_from_vmm(cache_size, 0, NULL, NULL);
}

INIT_ROUTINE_DEFINE(slab, INIT_ROUTINE_FLAGS_NONE, slab_init, vmm);
INIT_ROUTINE_DEFINE(slab_early, INIT_ROUTINE_FLAGS_NONE, slab_early_init, pmm);
