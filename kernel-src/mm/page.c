#include <kernel/page.h>
#include <limine.h>
#include <logging.h>
#include <string.h>
#include <arch/mmu.h>
#include <mutex.h>
#include <util.h>
#include <kernel/mm.h>
#include <kernel/init.h>

uintptr_t hhdm_base;
static size_t memory_size;
static size_t page_count;
size_t mm_free_page_count;

static mutex_t free_list_mutex;
static page_t *free_lists[MEMORY_SECTION_COUNT];
static page_t *free_tails[MEMORY_SECTION_COUNT];
static page_t *standby_lists[MEMORY_SECTION_COUNT];
static page_t *standby_tails[MEMORY_SECTION_COUNT];

typedef struct {
	uintmax_t base_id;
	uintmax_t top_id;
	uintmax_t search_start;
} section_t;

#define TOP_1MB (0x100000 / PAGE_SIZE)
#define TOP_4GB ((uint64_t)0x100000000 / PAGE_SIZE)

static section_t sections[MEMORY_SECTION_COUNT] = {
	{0, TOP_1MB, 0},
	{TOP_1MB, TOP_4GB, TOP_1MB},
	{TOP_4GB, 0xffffffffffffffffl, TOP_4GB}
};

static volatile struct limine_hhdm_request hhdm_req = {
	.id = LIMINE_HHDM_REQUEST_ID,
	.revision = 0
};

volatile struct limine_memmap_request mm_page_limine_map = {
	.id = LIMINE_MEMMAP_REQUEST_ID,
	.revision = 0
};

static page_t* pages;

#define PAGE_GET_ID(page) (((uintptr_t)(page) - (uintptr_t)pages) / sizeof(page_t))
#define PAGE_BOUNDARY_CHECK(page_id) \
	__assert((page_id) * PAGE_SIZE < (uintptr_t)pages || (page_id) * PAGE_SIZE >= (uintptr_t)&pages[page_count])

static void insert_in_free_list(page_t *page) {
	uintmax_t page_id = PAGE_GET_ID(page);
	PAGE_BOUNDARY_CHECK(page_id);
	struct page_t **list;
	struct page_t **tail;

	int section;

	if (page_id < TOP_1MB)
		section = MEMORY_SECTION_1MB;
	else if (page_id < TOP_4GB)
		section = MEMORY_SECTION_4GB;
	else
		section = MEMORY_SECTION_DEFAULT;

	list = page->backing ? &standby_lists[section] : &free_lists[section];
	tail = page->backing ? &standby_tails[section] : &free_tails[section];

	if (sections[section].search_start > page_id)
		sections[section].search_start = page_id;

	page->free_next = *list;
	page->free_prev = NULL;
	*list = page;
	if (page->free_next)
		page->free_next->free_prev = page;
	else
		*tail = page;

	++mm_free_page_count;
}

static void remove_from_free_list(page_t *page) {
	uintmax_t page_id = PAGE_GET_ID(page);
	PAGE_BOUNDARY_CHECK(page_id);
	struct page_t **list;
	struct page_t **tail;

	int section;

	if (page_id < TOP_1MB)
		section = MEMORY_SECTION_1MB;
	else if (page_id < TOP_4GB)
		section = MEMORY_SECTION_4GB;
	else
		section = MEMORY_SECTION_DEFAULT;

	list = page->backing ? &standby_lists[section] : &free_lists[section];
	tail = page->backing ? &standby_tails[section] : &free_tails[section];

	if (page->free_prev)
		page->free_prev->free_next = page->free_next;
	else
		*list = page->free_next;

	if (page->free_next)
		page->free_next->free_prev = page->free_prev;
	else
		*tail = page->free_prev;

	page->free_next = NULL;
	page->free_prev = NULL;

	--mm_free_page_count;
}

static void internal_hold(page_t *page) {
	if (++page->refcount == 1) {
		// this is only valid on standby pages, in case of free pages its an use after free
		__assert((page->flags & PAGE_FLAGS_FREE) == 0);
		remove_from_free_list(page);
	}
}

page_t *mm_get_page(void *address) {
	return &pages[((uintptr_t)address / PAGE_SIZE)];
}

void *mm_get_page_address(page_t *page) {
	return (void *)(PAGE_GET_ID(page) * PAGE_SIZE);
}

void mm_hold_page(void *addr) {
	page_t *page = &pages[((uintptr_t)addr / PAGE_SIZE)];

	MUTEX_ACQUIRE(&free_list_mutex);
	internal_hold(page);
	MUTEX_RELEASE(&free_list_mutex);
}

void mm_release_page(void *addr) {
	page_t *page = &pages[(uintptr_t)addr / PAGE_SIZE];

	MUTEX_ACQUIRE(&free_list_mutex);
	__assert(page->refcount != 0);
	if (--page->refcount == 0) {
		__assert(!mm_is_page_locked(page));
		__assert((page->flags & PAGE_FLAGS_DIRTY) == 0);
		insert_in_free_list(page);
		if (page->backing == NULL)
			page->flags |= PAGE_FLAGS_FREE;
	}
	MUTEX_RELEASE(&free_list_mutex);
}

void mm_unlock_and_release_page(void *addr) {
	page_t *page = &pages[(uintptr_t)addr / PAGE_SIZE];
	mm_unlock_page(page);
	mm_release_page(addr);
}

bool mm_is_page_locked(page_t *page) {
	return __atomic_load_n(&page->lock_count, __ATOMIC_SEQ_CST) > 0;
}

void mm_unlock_page(page_t *page) {
	__assert(__atomic_fetch_sub(&page->lock_count, 1, __ATOMIC_SEQ_CST) > 0);
}

static void do_alloc(page_t *page) {
	__assert(page->refcount == 0);
	memset(page, 0, sizeof(page_t));
	page->refcount = 1;
}

void *mm_alloc_page(int section) {
	retry:
	MUTEX_ACQUIRE(&free_list_mutex);
	page_t *page = NULL;

	// try to take a free anonymous page
	for (int i = section; i >= 0; --i) {
		page = free_lists[i];
		if (page) {
			remove_from_free_list(page);
			__assert(page->refcount == 0);
			break;
		}
	}

	bool cache_page = false;

	// if that wasn't possible, try to take from the cache standby list
	if (page == NULL) {
		for (int i = section; i >= 0; --i) {
			page = standby_tails[i];
			if (page) {
				cache_page = true;
				internal_hold(page);
				break;
			}
		}
	}

	MUTEX_RELEASE(&free_list_mutex);

	if (cache_page && mm_cache_take_page(page) == EAGAIN) {
		// someone already got the page from the cache between us holding it and taking it
		mm_release_page(mm_get_page_address(page));
		page = NULL;
		// retry it from the start, as an anonymous page could have been released while the lock was not held
		goto retry;
	} else if (cache_page) {
		// we got the page from the cache, all is good and we hold the only reference to it.
		// set the refcount to 0, as expected by the do_alloc call
		page->refcount = 0;
	}

	void *address = NULL;
	if (page) {
		address = (void *)(PAGE_GET_ID(page) * PAGE_SIZE);
		do_alloc(page);
	}

	return address;
}

void mm_force_free_page(void *address, size_t count) {
	MUTEX_ACQUIRE(&free_list_mutex);
	memory_size += PAGE_SIZE * count;
	__assert(((uintptr_t)address % PAGE_SIZE) == 0);
	uintmax_t base_id = (uintptr_t)address / PAGE_SIZE;
	for (int i = 0; i < count; ++i) {
		uintmax_t page_id = base_id + i;
		PAGE_BOUNDARY_CHECK(page_id);
		page_t *page = &pages[page_id];
		page->flags |= PAGE_FLAGS_FREE;
		insert_in_free_list(page);
	}
	MUTEX_RELEASE(&free_list_mutex);
}

void mm_page_init() {
	__assert(hhdm_req.response);
	hhdm_base = hhdm_req.response->offset;
	__assert(mm_page_limine_map.response);

	// get size of memory, top of usable memory, biggest section and print memory map
	size_t top = 0;
	struct limine_memmap_entry *biggest = NULL;
	printf("mm: ranges:\n");
	for (size_t i = 0; i < mm_page_limine_map.response->entry_count; ++i) {
		struct limine_memmap_entry *e = mm_page_limine_map.response->entries[i];
		printf("mm: %016p -> %016p: %d\n", e->base, e->base + e->length, e->type);
		if (e->type == LIMINE_MEMMAP_USABLE || e->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE || e->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) {
			size_t section_top = e->base + e->length;
			if (section_top > top)
				top = section_top;
		}
		if (e->type == LIMINE_MEMMAP_USABLE) {
			if (biggest == NULL || e->length > biggest->length)
				biggest = e;
			memory_size += e->length;
		}
	}

	pages = MAKE_HHDM((page_t *)biggest->base);
	page_count = ROUND_UP(top, PAGE_SIZE) / PAGE_SIZE;
	memset(pages, 0, page_count * sizeof(page_t));
	printf("mm: %d pages used for page list\n", ROUND_UP(page_count * sizeof(page_t), PAGE_SIZE) / PAGE_SIZE);

	// initialize usable memory
	for (size_t i = 0; i < mm_page_limine_map.response->entry_count; ++i) {
		struct limine_memmap_entry *e = mm_page_limine_map.response->entries[i];
		if (e->type == LIMINE_MEMMAP_USABLE) {
			int first_usable_page = e == biggest ? ROUND_UP(e->base + page_count * sizeof(page_t), PAGE_SIZE) / PAGE_SIZE : e->base / PAGE_SIZE;
			for (int i = first_usable_page; i < (e->base + e->length) / PAGE_SIZE; ++i) {
				pages[i].flags |= PAGE_FLAGS_FREE;
				insert_in_free_list(&pages[i]);
			}
		} else if (e->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
			for (int i = 0; i < e->length / PAGE_SIZE; ++i) {
				page_t *page = &pages[(e->base / PAGE_SIZE) + i];
				page->refcount = 1;
			}
		}
	}

	MUTEX_INIT(&free_list_mutex);
}

INIT_ROUTINE_DEFINE(mm_page, INIT_ROUTINE_FLAGS_NONE, mm_page_init, arch_early);

// XXX mm_alloc_pages won't be able to take pages from the page cache when the allocation size is over 1 page

void *mm_alloc_pages(size_t size, int section) {
	__assert(size);
	// mm_alloc_page is more suited for single page allocations, so use that instead
	if (size == 1)
		return mm_alloc_page(section);

	MUTEX_ACQUIRE(&free_list_mutex);

	uintmax_t page = 0;
	size_t found = 0;

	for (; section >= 0; --section) {
		for (page = sections[section].search_start; page < sections[section].top_id; ++page) {
			if (page >= page_count)
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
			remove_from_free_list(&pages[page + i]);
			do_alloc(&pages[page + i]);
		}
	}

	MUTEX_RELEASE(&free_list_mutex);
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
	*total_pages = memory_size / PAGE_SIZE;
	*free_pages = mm_free_page_count;
}
