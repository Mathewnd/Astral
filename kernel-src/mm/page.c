#include <kernel/page.h>
#include <limine.h>
#include <logging.h>
#include <string.h>
#include <arch/mmu.h>
#include <mutex.h>
#include <util.h>
#include <kernel/vmmcache.h>
#include <kernel/init.h>

uintptr_t hhdmbase;
static size_t memorysize;
static size_t pagecount;
size_t freepagecount;

static mutex_t freelistmutex;
static page_t *freelists[MEMORY_SECTION_COUNT];
static page_t *freetails[MEMORY_SECTION_COUNT];
static page_t *standbylists[MEMORY_SECTION_COUNT];
static page_t *standbytails[MEMORY_SECTION_COUNT];

typedef struct {
	uintmax_t baseid;
	uintmax_t topid;
	uintmax_t searchstart;
} section_t;

#define TOP_1MB (0x100000 / PAGE_SIZE)
#define TOP_4GB ((uint64_t)0x100000000 / PAGE_SIZE)

static section_t sections[MEMORY_SECTION_COUNT] = {
	{0, TOP_1MB, 0},
	{TOP_1MB, TOP_4GB, TOP_1MB},
	{TOP_4GB, 0xffffffffffffffffl, TOP_4GB}
};

static volatile struct limine_hhdm_request hhdmreq = {
	.id = LIMINE_HHDM_REQUEST_ID,
	.revision = 0
};

volatile struct limine_memmap_request mm_page_limine_map = {
	.id = LIMINE_MEMMAP_REQUEST_ID,
	.revision = 0
};

static page_t* pages;

#define PAGE_GETID(page) (((uintptr_t)(page) - (uintptr_t)pages) / sizeof(page_t))
#define PAGE_BOUNDARYCHECK(pageid) \
	__assert((pageid) * PAGE_SIZE < (uintptr_t)pages || (pageid) * PAGE_SIZE >= (uintptr_t)&pages[pagecount])

static void insertinfreelist(page_t *page) {
	uintmax_t pageid = PAGE_GETID(page);
	PAGE_BOUNDARYCHECK(pageid);
	struct page_t **list;
	struct page_t **tail;

	int section;

	if (pageid < TOP_1MB)
		section = MEMORY_SECTION_1MB;
	else if (pageid < TOP_4GB)
		section = MEMORY_SECTION_4GB;
	else
		section = MEMORY_SECTION_DEFAULT;

	list = page->backing ? &standbylists[section] : &freelists[section];
	tail = page->backing ? &standbytails[section] : &freetails[section];

	if (sections[section].searchstart > pageid)
		sections[section].searchstart = pageid;

	page->freenext = *list;
	page->freeprev = NULL;
	*list = page;
	if (page->freenext)
		page->freenext->freeprev = page;
	else
		*tail = page;

	++freepagecount;
}

static void removefromfreelist(page_t *page) {
	uintmax_t pageid = PAGE_GETID(page);
	PAGE_BOUNDARYCHECK(pageid);
	struct page_t **list;
	struct page_t **tail;

	int section;

	if (pageid < TOP_1MB)
		section = MEMORY_SECTION_1MB;
	else if (pageid < TOP_4GB)
		section = MEMORY_SECTION_4GB;
	else
		section = MEMORY_SECTION_DEFAULT;

	list = page->backing ? &standbylists[section] : &freelists[section];
	tail = page->backing ? &standbytails[section] : &freetails[section];

	if (page->freeprev)
		page->freeprev->freenext = page->freenext;
	else
		*list = page->freenext;

	if (page->freenext)
		page->freenext->freeprev = page->freeprev;
	else
		*tail = page->freeprev;

	page->freenext = NULL;
	page->freeprev = NULL;

	--freepagecount;
}

static void internalhold(page_t *page) {
	if (++page->refcount == 1) {
		// this is only valid on standby pages, in case of free pages its an use after free
		__assert((page->flags & PAGE_FLAGS_FREE) == 0);
		removefromfreelist(page);
	}
}

page_t *mm_get_page(void *address) {
	return &pages[((uintptr_t)address / PAGE_SIZE)];
}

void *mm_get_page_address(page_t *page) {
	return (void *)(PAGE_GETID(page) * PAGE_SIZE);
}

void mm_hold_page(void *addr) {
	page_t *page = &pages[((uintptr_t)addr / PAGE_SIZE)];

	MUTEX_ACQUIRE(&freelistmutex);
	internalhold(page);
	MUTEX_RELEASE(&freelistmutex);
}

void mm_release_page(void *addr) {
	page_t *page = &pages[(uintptr_t)addr / PAGE_SIZE];

	MUTEX_ACQUIRE(&freelistmutex);
	__assert(page->refcount != 0);
	if (--page->refcount == 0) {
		__assert((page->flags & PAGE_FLAGS_DIRTY) == 0);
		insertinfreelist(page);
		if (page->backing == NULL)
			page->flags |= PAGE_FLAGS_FREE;
	}
	MUTEX_RELEASE(&freelistmutex);
}

static void doalloc(page_t *page) {
	__assert(page->refcount == 0);
	memset(page, 0, sizeof(page_t));
	page->refcount = 1;
}

void *mm_alloc_page(int section) {
	retry:
	MUTEX_ACQUIRE(&freelistmutex);
	page_t *page = NULL;

	// try to take a free anonymous page
	for (int i = section; i >= 0; --i) {
		page = freelists[i];
		if (page) {
			removefromfreelist(page);
			__assert(page->refcount == 0);
			break;
		}
	}

	bool cachepage = false;

	// if that wasn't possible, try to take from the cache standby list
	if (page == NULL) {
		for (int i = section; i >= 0; --i) {
			page = standbytails[i];
			if (page) {
				cachepage = true;
				internalhold(page);
				break;
			}
		}
	}

	MUTEX_RELEASE(&freelistmutex);

	if (cachepage && vmmcache_takepage(page) == EAGAIN) {
		// someone already got the page from the cache between us holding it and taking it
		mm_release_page(mm_get_page_address(page));
		page = NULL;
		// retry it from the start, as an anonymous page could have been released while the lock was not held
		goto retry;
	} else if (cachepage) {
		// we got the page from the cache, all is good and we hold the only reference to it.
		// set the refcount to 0, as expected by the doalloc call
		page->refcount = 0;
	}

	void *address = NULL;
	if (page) {
		address = (void *)(PAGE_GETID(page) * PAGE_SIZE);
		doalloc(page);
	}

	return address;
}

void mm_force_free_page(void *address, size_t count) {
	MUTEX_ACQUIRE(&freelistmutex);
	memorysize += PAGE_SIZE * count;
	__assert(((uintptr_t)address % PAGE_SIZE) == 0);
	uintmax_t baseid = (uintptr_t)address / PAGE_SIZE;
	for (int i = 0; i < count; ++i) {
		uintmax_t pageid = baseid + i;
		PAGE_BOUNDARYCHECK(pageid);
		page_t *page = &pages[pageid];
		page->flags |= PAGE_FLAGS_FREE;
		insertinfreelist(page);
	}
	MUTEX_RELEASE(&freelistmutex);
}

void mm_page_init() {
	__assert(hhdmreq.response);
	hhdmbase = hhdmreq.response->offset;
	__assert(mm_page_limine_map.response);

	// get size of memory, top of usable memory, biggest section and print memory map
	size_t top = 0;
	struct limine_memmap_entry *biggest = NULL;
	printf("mm: ranges:\n");
	for (size_t i = 0; i < mm_page_limine_map.response->entry_count; ++i) {
		struct limine_memmap_entry *e = mm_page_limine_map.response->entries[i];
		printf("mm: %016p -> %016p: %d\n", e->base, e->base + e->length, e->type);
		if (e->type == LIMINE_MEMMAP_USABLE || e->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE || e->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) {
			size_t sectiontop = e->base + e->length;
			if (sectiontop > top)
				top = sectiontop;
		}
		if (e->type == LIMINE_MEMMAP_USABLE) {
			if (biggest == NULL || e->length > biggest->length)
				biggest = e;
			memorysize += e->length;
		}
	}

	pages = MAKE_HHDM((page_t *)biggest->base);
	pagecount = ROUND_UP(top, PAGE_SIZE) / PAGE_SIZE;
	memset(pages, 0, pagecount * sizeof(page_t));
	printf("mm: %d pages used for page list\n", ROUND_UP(pagecount * sizeof(page_t), PAGE_SIZE) / PAGE_SIZE);

	// initialize usable memory
	for (size_t i = 0; i < mm_page_limine_map.response->entry_count; ++i) {
		struct limine_memmap_entry *e = mm_page_limine_map.response->entries[i];
		if (e->type == LIMINE_MEMMAP_USABLE) {
			int firstusablepage = e == biggest ? ROUND_UP(e->base + pagecount * sizeof(page_t), PAGE_SIZE) / PAGE_SIZE : e->base / PAGE_SIZE;
			for (int i = firstusablepage; i < (e->base + e->length) / PAGE_SIZE; ++i) {
				pages[i].flags |= PAGE_FLAGS_FREE;
				insertinfreelist(&pages[i]);
			}
		} else if (e->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
			for (int i = 0; i < e->length / PAGE_SIZE; ++i) {
				page_t *page = &pages[(e->base / PAGE_SIZE) + i];
				page->refcount = 1;
			}
		}
	}

	MUTEX_INIT(&freelistmutex);
}

INIT_ROUTINE_DEFINE(mm_page, INIT_ROUTINE_FLAGS_NONE, mm_page_init, arch_early);

// XXX mm_alloc_pages won't be able to take pages from the page cache when the allocation size is over 1 page

void *mm_alloc_pages(size_t size, int section) {
	__assert(size);
	// mm_alloc_page is more suited for single page allocations, so use that instead
	if (size == 1)
		return mm_alloc_page(section);

	MUTEX_ACQUIRE(&freelistmutex);

	uintmax_t page = 0;
	size_t found = 0;

	for (; section >= 0; --section) {
		for (page = sections[section].searchstart; page < sections[section].topid; ++page) {
			if (page >= pagecount)
				break;

			if (pages[page].flags & PAGE_FLAGS_FREE) {
				++found;
			} else
				found = 0;

			if (found == size)
				goto gotpages;
		}
		found = 0;
	}

	gotpages:
	void *addr = NULL;
	if (found == size) {
		page = page - (found - 1);
		addr = (void *)(page * PAGE_SIZE);
		for (int i = 0; i < size; ++i) {
			removefromfreelist(&pages[page + i]);
			doalloc(&pages[page + i]);
		}
	}

	MUTEX_RELEASE(&freelistmutex);
	return addr;
}

void mm_release_range(void *addr, size_t size) {
	__assert(size);
	// release multiple pages at once
	__assert(((uintptr_t)addr % PAGE_SIZE) == 0);
	for (int i = 0; i < size; ++i)
		mm_release_page((void *)((uintptr_t)addr + PAGE_SIZE * i));
}

void mm_get_page_statistics(size_t *total_pages, size_t *free_pages) {
	*total_pages = memorysize / PAGE_SIZE;
	*free_pages = freepagecount;
}
