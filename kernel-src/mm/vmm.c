#include <kernel/vmm.h>
#include <logging.h>
#include <kernel/pmm.h>
#include <util.h>
#include <arch/cpu.h>
#include <limine.h>
#include <string.h>

#define RANGE_TOP(x) (void *)((uintptr_t)x->start + x->size)

static vmmcache_t *newcache() {
	// TODO initialize lock
	vmmcache_t *ptr = pmm_alloc(1, PMM_SECTION_DEFAULT);
	if (ptr == NULL)
		return NULL;
	ptr = MAKE_HHDM(ptr);
	ptr->header.freecount = VMM_RANGES_PER_CACHE; 
	ptr->header.firstfree = 0;
	ptr->header.next = NULL;

	for (uintmax_t i = 0; i < VMM_RANGES_PER_CACHE; ++i) {
		ptr->ranges[i].size = 0;
		ptr->ranges[i].next = NULL;
	}

	return ptr;
}

// entries are allocated directly from the pmm into these caches
static vmmcache_t *cachelist;

// only supposed to be called on locked, not fully used caches
static uintmax_t getentrynumber(vmmcache_t *cache) {
	for (uintmax_t i = cache->header.firstfree; i < VMM_RANGES_PER_CACHE; ++i) {
		if (cache->ranges[i].size == 0)
			return i;
	}
	__assert(!"get entry number called from full cache");
}

static vmmrange_t *allocrange() {
	vmmcache_t *cache = cachelist;
	vmmrange_t *range = NULL;
	while (cache) {
		// TODO lock
		if (cache->header.freecount > 0) {
			--cache->header.freecount;
			uintmax_t r = getentrynumber(cache);
			cache->header.firstfree = r + 1;
			range = &cache->ranges[r];
			range->size = -1; // set as allocated temporarily
			break;
		} else if (cache->header.next == NULL)
			cache->header.next = newcache();

		cache = cache->header.next;
		// TODO unlock
	}

	// TODO unlock cache

	return range;
}

// ranges are separated into kernel and user. the kernel has a temporary user context
vmmcontext_t vmm_kernelctx;
static vmmspace_t kernelspace = {
	.start = KERNELSPACE_START,
	.end = KERNELSPACE_END
};

// returns a pointer to the space of vaddr
static vmmspace_t *getspace(void *vaddr) {
	if (USERSPACE_START <= vaddr && vaddr <= USERSPACE_END)
		return &_cpu()->vmmctx->space;
	else if (KERNELSPACE_START <= vaddr && vaddr <= KERNELSPACE_END)
		return &kernelspace;
	else
		return NULL;
}


// get a range from an address
static vmmrange_t *getrange(vmmspace_t *space, void *addr) {
	vmmrange_t *range = space->ranges;
	while (range) {
		if (addr >= range->start && addr < RANGE_TOP(range))
			break;
		range = range->next;
	}
	return range;
}

// get start of range that fits specific size from specific offset
static void *getfreerange(vmmspace_t *space, void *addr, size_t size) {
	vmmrange_t *range = space->ranges;
	if (addr == NULL)
		addr = space->start;

	// if theres no ranges
	if (range == NULL)
		return addr;

	// if theres free space before the first range
	if (range->start != space->start && addr < range->start && (uintptr_t)range->start - (uintptr_t)addr >= size)
		return addr;

	while (range->next) {
		void *rangetop = RANGE_TOP(range);
		if (addr < rangetop)
			addr = rangetop;

		if (addr < range->next->start) {
			size_t freesize = (uintptr_t)range->next->start - (uintptr_t)addr;
			if (freesize >= size)
				return addr;
		}
		range = range->next;
	}

	// if theres free space after the last range
	void *rangetop = RANGE_TOP(range);
	if (addr < rangetop)
		addr = rangetop;

	if (addr != space->end && (uintptr_t)space->end - (uintptr_t)addr >= size)
		return addr;

	return NULL;
}

static void insertrange(vmmspace_t *space, vmmrange_t *newrange) {
	// TODO better handle fragmentation
	vmmrange_t *range = space->ranges;

	if (range == NULL) {
		space->ranges = newrange;
		newrange->next = NULL;
		newrange->prev = NULL;
		return;
	}

	if (RANGE_TOP(newrange) < range->start) {
		space->ranges = newrange;
		newrange->next = range;
		range->prev = newrange;
		return;
	}

	while (range->next) {
		if (newrange->start >= RANGE_TOP(range) && newrange->start < range->next->start) {
			newrange->next = range->next;
			newrange->prev = range;
			range->next = newrange;
			return;
		}
		range = range->next;
	}

	range->next = newrange;
	newrange->prev = range;
	newrange->next = NULL;
}

static void destroyrange(vmmrange_t *range, uintmax_t _offset, size_t size) {
	uintmax_t top = _offset + size;
	__assert(!((range->flags & VMM_FLAGS_FILE) || (range->flags & VMM_FLAGS_COPY))); // unimplemented
	for (uintmax_t offset = _offset; offset < top; offset += PAGE_SIZE) {
		void *vaddr = (void *)((uintptr_t)range->start + offset);
		void *physical = arch_mmu_getphysical(_cpu()->vmmctx->pagetable, vaddr);
		if (physical) {
			pmm_free(physical, 1);
			arch_mmu_unmap(_cpu()->vmmctx->pagetable, vaddr);
		}
	}
}

static void unmap(vmmspace_t *space, void *address, size_t size) {
	void *top = (void *)((uintptr_t)address + size);
	vmmrange_t *range = space->ranges;

	while (range) {
		void *rangetop = RANGE_TOP(range);
		// completely unmapped
		if (range->start >= address && rangetop <= top) {
			// TODO unallocate entry
			if (range->prev)
				range->prev->next = range->next;
			else
				space->ranges = range->next;

			if (range->next)
				range->next->prev = range->prev;

			destroyrange(range, 0, range->size);
		} else if (address > range->start && top < rangetop) { // split
			vmmrange_t *new = allocrange();
			__assert(new); // XXX deal with this better
			*new = *range; // XXX refcounts will break

			destroyrange(range, (uintptr_t)address - (uintptr_t)range->start, size);

			new->start = top;
			new->size = (uintptr_t)rangetop - (uintptr_t)new->start;
			range->size = (uintptr_t)address - (uintptr_t)range->start;

			new->prev = range;
			range->next = new;
		} else if (top > range->start && range->start >= address) { // partially unmap from start
			size_t difference = (uintptr_t)top - (uintptr_t)range->start;
			destroyrange(range, 0, difference);
			range->start = (void *)((uintptr_t)range->start + difference);
			range->size -= difference;
		} else if (address < rangetop && rangetop <= top){ // partially unmap from end
			size_t difference = (uintptr_t)rangetop - (uintptr_t)address;
			range->size -= difference;
			destroyrange(range, range->size, difference);
		}

		range = range->next;
	}

}

bool vmm_pagefault(void *addr, bool user, int actions) {
	if (user == false)
		return false;

	addr = (void *)ROUND_DOWN((uintptr_t)addr, PAGE_SIZE);

	vmmspace_t *space = getspace(addr);

	if (space == NULL || (space == &kernelspace && user))
		return false;

	// TODO lock
	vmmrange_t *range = getrange(space, addr);

	bool status = false;

	if (range == NULL)
		goto cleanup;

	// check if valid

	int invalidactions = 0;

	if ((range->mmuflags & ARCH_MMU_FLAG_READ) == 0)
		invalidactions |= VMM_ACTION_READ;
	
	if ((range->mmuflags & ARCH_MMU_FLAG_WRITE) == 0)
		invalidactions |= VMM_ACTION_WRITE;
	
	if ((range->mmuflags & ARCH_MMU_FLAG_NOEXEC))
		invalidactions |= VMM_ACTION_EXEC;

	if (invalidactions & actions)
		goto cleanup;

	// TODO file/CoW

	void *paddr = pmm_alloc(1, PMM_SECTION_DEFAULT);
	__assert(paddr); // XXX handle these better than an assert once there are signals and such
	__assert(arch_mmu_map(_cpu()->vmmctx->pagetable, paddr, addr, range->mmuflags));
	memset(addr, 0, PAGE_SIZE);

	status = true;

	cleanup:
	// TODO unlock
	return status;
}

void *vmm_map(void *addr, volatile size_t size, int flags, mmuflags_t mmuflags, void *private) {
	if (addr == NULL)
		addr = KERNELSPACE_START;

	addr = (void *)ROUND_DOWN((uintptr_t)addr, PAGE_SIZE);
	if (flags & VMM_FLAGS_PAGESIZE)
		size *= PAGE_SIZE;
	else
		size = ROUND_UP(size, PAGE_SIZE);

	if (size == 0)
		return NULL;

	vmmspace_t *space = getspace(addr);
	if (space == NULL)
		return NULL;

	// TODO lock space

	void *start = getfreerange(space, addr, size);
	void *retaddr = NULL;
	if (((flags & VMM_FLAGS_EXACT) && start != addr) || start == NULL)
		goto cleanup;

	vmmrange_t *range = allocrange();
	if (range == NULL)
		goto cleanup;

	range->start = start;
	range->size = size;
	range->flags = VMM_PERMANENT_FLAGS_MASK & flags;
	range->mmuflags = mmuflags;

	// TODO file mapping and CoW

	if (flags & VMM_FLAGS_PHYSICAL) {
		// map to allocated virtual memory
		for (uintmax_t i = 0; i < size; i += PAGE_SIZE) {
			if (arch_mmu_map(_cpu()->vmmctx->pagetable, (void *)((uintptr_t)private + i), (void *)((uintptr_t)start + i), mmuflags) == false) {
				for (uintmax_t j = 0; j < size; j += PAGE_SIZE)
					arch_mmu_unmap(_cpu()->vmmctx->pagetable, (void *)((uintptr_t)start + j));

				goto cleanup;
			}
		}
	} else if (flags & VMM_FLAGS_ALLOCATE) {
		// allocate to virtual memory
		for (uintmax_t i = 0; i < size; i += PAGE_SIZE) {
			void *allocated = pmm_alloc(1, PMM_SECTION_DEFAULT);
			if (arch_mmu_map(_cpu()->vmmctx->pagetable, allocated, (void *)((uintptr_t)start + i), mmuflags) == false) {
				for (uintmax_t j = 0; j < size; j += PAGE_SIZE) {
						void *virt = (void *)((uintptr_t)start + i);
						void *physical = arch_mmu_getphysical(_cpu()->vmmctx->pagetable, virt);

						if (physical) {
							pmm_free(physical, 1);
							arch_mmu_unmap(_cpu()->vmmctx->pagetable, virt);
						}
				}

				goto cleanup;
			}
			memset(MAKE_HHDM(allocated), 0, PAGE_SIZE);
		}
	}

	insertrange(space, range);

	retaddr = start;

	cleanup:
	if (start == NULL && range) {
		// TODO free range
	}
	// TODO unlock
	return retaddr;
}

void vmm_unmap(void *addr, size_t size, int flags) {
	addr = (void *)ROUND_DOWN((uintptr_t)addr, PAGE_SIZE);
	if (flags & VMM_FLAGS_PAGESIZE)
		size *= PAGE_SIZE;
	else
		size = ROUND_UP(size, PAGE_SIZE);

	if (size == 0)
		return;

	vmmspace_t *space = getspace(addr);
	if (space == NULL)
		return;

	// TODO lock space

	unmap(space, addr, size);

	// TODO unlock space
}

vmmcontext_t *vmm_newcontext() {
	__assert(!"not yet");
}

void vmm_switchcontext(vmmcontext_t *ctx) {
	arch_mmu_switch(ctx->pagetable);
	_cpu()->vmmctx = ctx;
}

static void printspace(vmmspace_t *space) {
	printf("vmm: ranges:\n");
	vmmrange_t *range = space->ranges;
	while (range) {
		printf("vmm: address %p size %lx flags %x\n", range->start, range->size, range->flags);
		range = range->next;
	}
}

extern void *_text_start;
extern void *_data_start;
extern void *_rodata_start;
extern void *_text_end;
extern void *_data_end;
extern void *_rodata_end;

extern volatile struct limine_memmap_request pmm_liminemap;

void vmm_init() {
	__assert(sizeof(vmmcache_t) <= PAGE_SIZE);

	cachelist = newcache();
	vmm_kernelctx.pagetable = arch_mmu_newtable();
	__assert(cachelist && vmm_kernelctx.pagetable);

	vmm_kernelctx.space.start = USERSPACE_START;
	vmm_kernelctx.space.end = USERSPACE_END;

	vmm_switchcontext(&vmm_kernelctx);

	for (uint64_t i = 0; i < pmm_liminemap.response->entry_count; ++i) {
		struct limine_memmap_entry *e = pmm_liminemap.response->entries[i];
		__assert(vmm_map(MAKE_HHDM(e->base), e->length, 0, ARCH_MMU_FLAG_READ | ARCH_MMU_FLAG_WRITE | ARCH_MMU_FLAG_NOEXEC, NULL));
	}

	__assert(vmm_map(&_text_start, (uintptr_t)&_text_end - (uintptr_t)&_text_start, 0, ARCH_MMU_FLAG_READ, NULL));
	__assert(vmm_map(&_rodata_start, (uintptr_t)&_rodata_end - (uintptr_t)&_rodata_start, 0, ARCH_MMU_FLAG_READ | ARCH_MMU_FLAG_NOEXEC, NULL));
	__assert(vmm_map(&_data_start, (uintptr_t)&_data_end - (uintptr_t)&_data_start, 0, ARCH_MMU_FLAG_READ | ARCH_MMU_FLAG_WRITE | ARCH_MMU_FLAG_NOEXEC, NULL));

	printspace(&kernelspace);
}
