#include <kernel/pmm.h>
#include <limine.h>
#include <logging.h>
#include <string.h>
#include <arch/mmu.h>
#include <spinlock.h>

uintptr_t hhdmbase;
static size_t memorysize;
static size_t physicalpagecount;

typedef struct {
	spinlock_t lock;
	uintmax_t base;
	uintmax_t top;
	uintmax_t searchstart;
} bmsection_t;

static bmsection_t bmsections[PMM_SECTION_COUNT];

static uint64_t *bitmap;

static volatile struct limine_hhdm_request hhdmreq = {
	.id = LIMINE_HHDM_REQUEST,
	.revision = 0
};

volatile struct limine_memmap_request pmm_liminemap = {
	.id = LIMINE_MEMMAP_REQUEST,
	.revision = 0
};

static inline void bmset(void *addr, uint64_t v) {
	uintmax_t page = (uintptr_t)addr / PAGE_SIZE;
	uintmax_t entry = page / 64;
	uintmax_t offset = page % 64;

	uint64_t entryval = bitmap[entry];
	entryval &= ~((uint64_t)1 << offset);
	entryval |= v << offset;
	bitmap[entry] = entryval;
}

static uintmax_t getfreearea(bmsection_t *section, size_t size) {
	uintmax_t result = 0;
	uintmax_t found = 0;

	for (uintmax_t search = section->searchstart; search < section->top && found < size; ++search) {
		uintmax_t entry = search / 64;
		uintmax_t offset = search % 64;

		if (bitmap[entry] == 0) {
			search += 63 - offset; // 63 because search gets incremented on continue
			result = 0;
			found = 0;
			continue;
		}

		if (((bitmap[entry] >> offset) & 1) == 0) {
			found = 0;
			result = 0;
			continue;
		}

		if (found == 0)
			result = search;

		++found;
	}

	return result;
}

static inline void initsection(int section, uintmax_t base, uintmax_t top) {
	bmsections[section] = (bmsection_t){
		.base = base,
		.top = top,
		.searchstart = base
	};
	SPINLOCK_INIT(bmsections[section].lock);
}

void *pmm_alloc(size_t size, int section) {
	for (int i = section; i >= 0; --i) {
		spinlock_acquire(&bmsections[i].lock);
		uintmax_t page = getfreearea(&bmsections[i], size);

		if (page) {
			if (page == bmsections[i].searchstart)
				bmsections[i].searchstart += size;

			for (size_t j = 0; j < size; ++j)
				bmset((void *)((page + j) * PAGE_SIZE), 0);

			spinlock_release(&bmsections[i].lock);
			return (void *)(page * PAGE_SIZE);
		}
		spinlock_release(&bmsections[i].lock);
	}
	return NULL;
}

void pmm_free(void *addr, size_t size) {
	uintmax_t page = (uintptr_t)addr / PAGE_SIZE;

	for (int i = 0; i < PMM_SECTION_COUNT; ++i) {
		if (page >= bmsections[i].base && page < bmsections[i].top) {
			spinlock_acquire(&bmsections[i].lock);

			if ((uintptr_t)addr / PAGE_SIZE < bmsections[i].searchstart)
				bmsections[i].searchstart = (uintptr_t)addr / PAGE_SIZE;

			for (uintmax_t j = 0; j < size; ++j)
				bmset((void *)((page + j) * PAGE_SIZE), 1);

			spinlock_release(&bmsections[i].lock);
			return;
		}
	}
}

#define TOP_1MB (0x100000 / PAGE_SIZE)
#define TOP_4GB ((uint64_t)0x100000000 / PAGE_SIZE)

void pmm_init() {
	__assert(hhdmreq.response);
	hhdmbase = hhdmreq.response->offset;
	__assert(pmm_liminemap.response);

	uintmax_t topofmemory = 0;

	// get size of memory and print memory map
	printf("memory map:\n");
	for (size_t i = 0; i < pmm_liminemap.response->entry_count; ++i) {
		struct limine_memmap_entry *e = pmm_liminemap.response->entries[i];
		printf("%016p -> %016p: %d\n", e->base, e->base + e->length, e->type);
		if (e->type == LIMINE_MEMMAP_USABLE) {
			topofmemory = e->base + e->length;
			memorysize += e->length;
		}
	}

	physicalpagecount = topofmemory / PAGE_SIZE;

	size_t bitmapsize = physicalpagecount / 8;

	// find out where to put the bitmap
	for (uintmax_t i = 0; i < pmm_liminemap.response->entry_count; ++i) {
		struct limine_memmap_entry *e = pmm_liminemap.response->entries[i];
		if (e->type == LIMINE_MEMMAP_USABLE && e->length >= bitmapsize)
			bitmap = (uint64_t *)e->base;
	}
	__assert(bitmap);

	memset(bitmap, 0, bitmapsize);

	// set free pages
	for (uintmax_t i = 0; i < pmm_liminemap.response->entry_count; ++i) {
		struct limine_memmap_entry *e = pmm_liminemap.response->entries[i];
		if (e->type != LIMINE_MEMMAP_USABLE)
			continue;

		for (uintmax_t offset = 0; offset < e->length; offset += PAGE_SIZE)
			bmset((void *)(e->base + offset), 1);
	}

	// set bitmap as used
	for (uintmax_t offset = 0; offset < bitmapsize; offset += PAGE_SIZE)
		bmset((void *)((uintptr_t)bitmap + offset), 0);

	// prepare sections
	// for all cases this section will always be present so hardcode its values (otherwise the system wouldn't even get here)
	initsection(PMM_SECTION_1MB, 1, TOP_1MB);
	initsection(PMM_SECTION_4GB, TOP_1MB, physicalpagecount > TOP_4GB ? TOP_4GB : physicalpagecount);
	initsection(PMM_SECTION_DEFAULT, TOP_4GB, physicalpagecount > TOP_4GB ? physicalpagecount : TOP_4GB);
	bitmap = MAKE_HHDM(bitmap);
}
