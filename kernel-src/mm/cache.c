#include <kernel/mm.h>
#include <semaphore.h>
#include <logging.h>
#include <kernel/init.h>

typedef struct {
	list_node_t list_node;
	semaphore_t semaphore;
	page_t *page;
} page_wait_block_t;

typedef struct {
	mutex_t mutex;
	list_t wait_block_list;
} page_wait_block_header_t;

#define WAIT_BLOCK_COUNT 128

static page_wait_block_header_t wait_blocks[WAIT_BLOCK_COUNT];
size_t mm_cache_cached_pages;

static page_wait_block_header_t *page_wait_pick_header(page_t *page) {
	return &wait_blocks[(uintptr_t)mm_get_page_address(page) / PAGE_SIZE % WAIT_BLOCK_COUNT]; // TODO: maybe use a hash
}

static void page_wait_attach(page_t *page, page_wait_block_t *page_wait_block) {
	page_wait_block_header_t *header = page_wait_pick_header(page);

	SEMAPHORE_INIT(&page_wait_block->semaphore, 0);
	page_wait_block->page = page;

	MUTEX_ACQUIRE(&header->mutex);
	list_push_back(&header->wait_block_list, &page_wait_block->list_node);
	MUTEX_RELEASE(&header->mutex);
}

static void page_wait_detach(page_wait_block_t *page_wait_block) {
	page_wait_block_header_t *header = page_wait_pick_header(page_wait_block->page);

	MUTEX_ACQUIRE(&header->mutex);
	list_remove(&header->wait_block_list, &page_wait_block->list_node);
	MUTEX_RELEASE(&header->mutex);
}

static void page_wait(page_wait_block_t *page_wait_block) {
	semaphore_wait(&page_wait_block->semaphore, false);
}

static void page_notify_waiters(page_t *page) {
	page_wait_block_header_t *header = page_wait_pick_header(page);

	MUTEX_ACQUIRE(&header->mutex);

	list_for_each(&header->wait_block_list, list_node) {
		page_wait_block_t *block = (page_wait_block_t *)list_node;

		if (block->page == page)
			semaphore_signal(&block->semaphore);
	}

	MUTEX_RELEASE(&header->mutex);
}

// expects vnode pushlock with shared+ acquire
static page_t *lookup_page(vnode_t *vnode, uintmax_t offset) {
	void *v;
	return trie_lookup(&vnode->pages, offset / PAGE_SIZE, &v) ? NULL : v;
}

// expects vnode pushlock with exclusive acquire
static int insert_page(vnode_t *vnode, uintmax_t offset, page_t *page, trie_preallocation_t *preallocation) {
	int error = trie_insert_preallocated(&vnode->pages, offset / PAGE_SIZE, page, preallocation);
	if (error == 0)
		__atomic_add_fetch(&mm_cache_cached_pages, 1, __ATOMIC_RELAXED);
	return error;
}

static void remove_page(vnode_t *vnode, uintmax_t offset) {
	__assert(trie_remove(&vnode->pages, offset / PAGE_SIZE) == 0);
	__atomic_sub_fetch(&mm_cache_cached_pages, 1, __ATOMIC_RELAXED);
}

static int wait_for_ready(page_t *page) {
	for (;;) {
		int flags = __atomic_load_n(&page->flags, __ATOMIC_ACQUIRE);
		if (flags & PAGE_FLAGS_READY) {
			if ((flags & PAGE_FLAGS_ERROR) == 0)
				return 0;

			void *phys = mm_get_page_address(page);
			mm_release_page(phys);
			return EIO;
		}

		page_wait_block_t page_wait_block;
		page_wait_attach(page, &page_wait_block);
		if (__atomic_load_n(&page->flags, __ATOMIC_RELAXED) & PAGE_FLAGS_READY) {
			page_wait_detach(&page_wait_block);
			continue;
		}

		page_wait(&page_wait_block);
		page_wait_detach(&page_wait_block);
	}
}

// TODO: clustered page in
int mm_cache_get_page(vnode_t *vnode, uintmax_t offset, int flags, page_t **res) {
	pushlock_acquire_shared(&vnode->pages_lock);

	page_t *page = lookup_page(vnode, offset);
	if (page)
		mm_hold_page(mm_get_page_address(page));

	pushlock_release_shared(&vnode->pages_lock);

retry_page:
	if (page) {
		*res = page;
		return wait_for_ready(page);
	} else if (flags & MM_CACHE_GET_PAGE_FLAGS_NO_POPULATE)
		return ENOENT;

	void *phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
	if (phys == NULL)
		return ENOMEM;

	page = mm_get_page(phys);

	trie_preallocation_t *trie_preallocation = trie_preallocate();
	if (trie_preallocation == NULL) {
		mm_release_page(phys);
		return ENOMEM;
	}

	pushlock_acquire_exclusive(&vnode->pages_lock);
	page_t *tmp = lookup_page(vnode, offset);
	if (tmp) {
		mm_hold_page(mm_get_page_address(tmp));
		pushlock_release_exclusive(&vnode->pages_lock);
		trie_free_preallocation(trie_preallocation);
		mm_release_page(phys);
		page = tmp;
		goto retry_page;
	}

	__assert(insert_page(vnode, offset, page, trie_preallocation) == 0);
	pushlock_release_exclusive(&vnode->pages_lock);

	VOP_LOCK(vnode);
	int error = VOP_GETPAGE(vnode, offset, page);
	VOP_UNLOCK(vnode);
	if (error) {
		pushlock_acquire_exclusive(&vnode->pages_lock);
		remove_page(vnode, offset);
		pushlock_release_exclusive(&vnode->pages_lock);
	} else {
		page->backing = vnode;
		page->offset = offset;
	}

	__atomic_or_fetch(&page->flags, PAGE_FLAGS_READY | (error ? PAGE_FLAGS_ERROR : 0), __ATOMIC_RELEASE);
	page_notify_waiters(page);
	*res = page;
	trie_free_preallocation(trie_preallocation);
	if (error) {
		mm_release_page(phys);
	}

	return error;
}

// removes a page from the page cache if only the caller holds a reference
// keeps the page in the same state as if it were in the page cache
int mm_cache_take_page(page_t *page) {
	pushlock_acquire_exclusive(&page->backing->pages_lock);
	if (page->refcount > 1) {
		pushlock_release_exclusive(&page->backing->pages_lock);
		return EAGAIN;
	}

	__assert((__atomic_load_n(&page->flags, __ATOMIC_RELAXED) & PAGE_FLAGS_DIRTY) == 0);
	// TODO: remove the need for this check (i.e. transform truncated pages into anonymous pages)
	if ((__atomic_load_n(&page->flags, __ATOMIC_RELAXED) & PAGE_FLAGS_TRUNCATED) == 0)
		remove_page(page->backing, page->offset);

	pushlock_release_exclusive(&page->backing->pages_lock);
	return 0;
}

static void truncate_iterate(void *p) {
	page_t *page = p;
	__atomic_sub_fetch(&mm_cache_cached_pages, 1, __ATOMIC_RELAXED);
	if (__atomic_or_fetch(&page->flags, PAGE_FLAGS_TRUNCATED, __ATOMIC_RELEASE) & PAGE_FLAGS_PINNED) {
		mm_release_page(mm_get_page_address(page));
	}
}

int mm_cache_truncate(vnode_t *vnode, uintmax_t offset) {
	uintmax_t min_page = ROUND_UP(offset, PAGE_SIZE) / PAGE_SIZE;

	pushlock_acquire_exclusive(&vnode->pages_lock);

	trie_iterate(&vnode->pages, min_page, UINT64_MAX, truncate_iterate);
	trie_truncate(&vnode->pages, min_page);

	pushlock_release_exclusive(&vnode->pages_lock);
	return 0;
}

static void mm_cache_init(void) {
	for (size_t i = 0; i < WAIT_BLOCK_COUNT; ++i) {
		MUTEX_INIT(&wait_blocks[i].mutex);
		list_init(&wait_blocks[i].wait_block_list);
	}

	mm_cache_init_writer();
}

INIT_ROUTINE_DEFINE(mm_cache, INIT_ROUTINE_FLAGS_NONE, mm_cache_init, scheduler, trie);
