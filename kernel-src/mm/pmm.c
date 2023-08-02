#include <kernel/pmm.h>
#include <limine.h>
#include <logging.h>
#include <string.h>
#include <arch/mmu.h>
#include <spinlock.h>
#include <util.h>

uintptr_t hhdmbase;
static size_t memorysize;
static size_t pagecount;

static spinlock_t freelistlock;
static page_t *freelists[PMM_SECTION_COUNT];
static page_t *standbylists[PMM_SECTION_COUNT];

typedef struct {
	uintmax_t baseid;
	uintmax_t topid;
	uintmax_t searchstart;
} section_t;

#define TOP_1MB (0x100000 / PAGE_SIZE)
#define TOP_4GB ((uint64_t)0x100000000 / PAGE_SIZE)

static section_t sections[PMM_SECTION_COUNT] = {
	{0, TOP_1MB, 0},
	{TOP_1MB, TOP_4GB, TOP_1MB},
	{TOP_4GB, 0xffffffffffffffffl, TOP_4GB}
};

static volatile struct limine_hhdm_request hhdmreq = {
	.id = LIMINE_HHDM_REQUEST,
	.revision = 0
};

volatile struct limine_memmap_request pmm_liminemap = {
	.id = LIMINE_MEMMAP_REQUEST,
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

	int section;

	if (pageid < TOP_1MB)
		section = PMM_SECTION_1MB;
	else if (pageid < TOP_4GB)
		section = PMM_SECTION_4GB;
	else
		section = PMM_SECTION_DEFAULT;

	list = page->backing ? &standbylists[section] : &freelists[section];

	if (sections[section].searchstart > pageid)
		sections[section].searchstart = pageid;

	page->freenext = *list;
	page->freeprev = NULL;
	*list = page;
	if (page->freenext)
		page->freenext->freeprev = page;
}

static void removefromfreelist(page_t *page) {
	uintmax_t pageid = PAGE_GETID(page);
	PAGE_BOUNDARYCHECK(pageid);
	struct page_t **list;

	int section;

	if (pageid < TOP_1MB)
		section = PMM_SECTION_1MB;
	else if (pageid < TOP_4GB)
		section = PMM_SECTION_4GB;
	else
		section = PMM_SECTION_DEFAULT;

	list = page->backing ? &standbylists[section] : &freelists[section];

	if (page->freeprev)
		page->freeprev->freenext = page->freenext;
	else
		*list = page->freenext;

	if (page->freenext)
		page->freenext->freeprev = page->freeprev;
}

void pmm_hold(void *addr) {
	page_t *page = &pages[((uintptr_t)addr / PAGE_SIZE)];
	__atomic_add_fetch(&page->refcount, 1, __ATOMIC_SEQ_CST);
}

void pmm_release(void *addr) {
	page_t *page = &pages[(uintptr_t)addr / PAGE_SIZE];
	__assert(page->refcount != 0);

	uintmax_t newrefcount = __atomic_sub_fetch(&page->refcount, 1, __ATOMIC_SEQ_CST);
	if (newrefcount == 0) {
		spinlock_acquire(&freelistlock);
		insertinfreelist(page);
		if (page->backing == NULL)
			page->flags |= PAGE_FLAGS_FREE;
		spinlock_release(&freelistlock);
	}
}

static void doalloc(page_t *page) {
	__assert(page->refcount == 0);
	memset(page, 0, sizeof(page_t));
	page->refcount = 1;
	page->flags &= ~PAGE_FLAGS_FREE;
}

void *pmm_allocpage(int section) {
	spinlock_acquire(&freelistlock);
	page_t *page = NULL;

	for (int i = section; i >= 0; --i) {
		page = freelists[i];
		if (page) {
			removefromfreelist(page);
			break;
		}
	}

	// TODO get page from standby list and call to remove from cache

	spinlock_release(&freelistlock);
	void *address = NULL;
	if (page) {
		address = (void *)(PAGE_GETID(page) * PAGE_SIZE);
		doalloc(page);
	}

	return address;
}

void pmm_makefree(void *address, size_t count) {
	memorysize += PAGE_SIZE * count;
	__assert(((uintptr_t)address % PAGE_SIZE) == 0);
	uintmax_t baseid = (uintptr_t)address / PAGE_SIZE;
	for (int i = 0; i < count; ++i) {
		uintmax_t pageid = baseid + i;
		PAGE_BOUNDARYCHECK(pageid);
		page_t *page = &pages[pageid];
		insertinfreelist(page);
	}
}

void pmm_init() {
	__assert(hhdmreq.response);
	hhdmbase = hhdmreq.response->offset;
	__assert(pmm_liminemap.response);

	// get size of memory, top of usable memory, biggest section and print memory map
	size_t top = 0;
	struct limine_memmap_entry *biggest = NULL;
	printf("pmm: ranges:\n");
	for (size_t i = 0; i < pmm_liminemap.response->entry_count; ++i) {
		struct limine_memmap_entry *e = pmm_liminemap.response->entries[i];
		printf("pmm: %016p -> %016p: %d\n", e->base, e->base + e->length, e->type);
		if (e->type == LIMINE_MEMMAP_USABLE || e->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
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
	printf("pmm: %d pages used for page list\n", ROUND_UP(pagecount * sizeof(page_t), PAGE_SIZE) / PAGE_SIZE);

	// place pages in free lists
	for (size_t i = 0; i < pmm_liminemap.response->entry_count; ++i) {
		struct limine_memmap_entry *e = pmm_liminemap.response->entries[i];
		if (e->type == LIMINE_MEMMAP_USABLE) {
			int firstusablepage = e == biggest ? ROUND_UP(e->base + pagecount * sizeof(page_t), PAGE_SIZE) / PAGE_SIZE : e->base / PAGE_SIZE;
			for (int i = firstusablepage; i < (e->base + e->length) / PAGE_SIZE; ++i) {
				pages[i].flags |= PAGE_FLAGS_FREE;
				insertinfreelist(&pages[i]);
			}
		}
	}

	SPINLOCK_INIT(freelistlock);
}

void *pmm_alloc(size_t size, int section) {
	__assert(size);
	// pmm_allocpage is more suited for single page allocations, so use that instead
	if (size == 1)
		return pmm_allocpage(section);

	spinlock_acquire(&freelistlock);

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

	spinlock_release(&freelistlock);
	return addr;
}

void pmm_free(void *addr, size_t size) {
	__assert(size);
	// release multiple pages at once
	__assert(((uintptr_t)addr % PAGE_SIZE) == 0);
	for (int i = 0; i < size; ++i)
		pmm_release((void *)((uintptr_t)addr + PAGE_SIZE * i));
}
