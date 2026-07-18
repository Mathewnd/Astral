#include <kernel/mm.h>
#include <util.h>
#include <logging.h>
#include <kernel/timekeeper.h>
#include <kernel/event.h>
#include <kernel/init.h>

#define TABLE_SIZE 4096
#define WRITER_TICK_SECONDS 15

static mutex_t mutex;
static page_t **table;

static thread_t *writer_thread;
static semaphore_t sync;
static eventheader_t sync_event;
static eventheader_t page_ready_event;
size_t mm_cache_cached_pages;

#define HOLD_LOCK() \
	MUTEX_ACQUIRE(&mutex);

#define RELEASE_LOCK() \
	MUTEX_RELEASE(&mutex);

static inline uint64_t fnv1a_hash(void *buffer, size_t size) {
	uint8_t *ptr = buffer;
	uint8_t *top = ptr + size;
	uint64_t h = FNV1OFFSET;

	while (ptr < top) {
		h ^= *ptr++;
		h *= FNV1PRIME;
	}

	return h;
}

static uintmax_t get_entry(vnode_t *vnode, uintmax_t offset) {
	struct {
		vnode_t *vnode;
		uintmax_t offset;
	} tmp;

	tmp.vnode = vnode;
	tmp.offset = offset;

	return fnv1a_hash(&tmp, sizeof(tmp)) % TABLE_SIZE;
}

// assumes lock is held
static page_t *find_page(vnode_t *vnode, uintmax_t offset) {
	uintmax_t entry = get_entry(vnode, offset);

	page_t *page = table[entry];
	while (page) {
		if (page->backing == vnode && page->offset == offset)
			break;
		page = page->hash_next;
	}

	return page;
}

// assumes lock is held
static void put_page(page_t *page) {
	uintmax_t entry = get_entry(page->backing, page->offset);
	// add to table list
	page->hash_prev = NULL;
	page->hash_next = table[entry];
	if (table[entry])
		table[entry]->hash_prev = page;

	table[entry] = page;

	// add to vnode list
	// XXX this could be a bit slow if a large portion of the file is in memory and it gets purged
	page->vnode_prev = NULL;
	page->vnode_next = page->backing->pages;
	if (page->backing->pages)
		page->backing->pages->vnode_prev = page;

	page->backing->pages = page;
	++mm_cache_cached_pages;
}

// assumes lock is held
static void remove_page(page_t *page) {
	uintmax_t entry = get_entry(page->backing, page->offset);

	// remove from table list
	if (page->hash_next)
		page->hash_next->hash_prev = page->hash_prev;

	if (page->hash_prev)
		page->hash_prev->hash_next = page->hash_next;
	else
		table[entry] = page->hash_next;

	page->hash_next = NULL;
	page->hash_prev = NULL;

	// remove from vnode list
	if (page->vnode_next)
		page->vnode_next->vnode_prev = page->vnode_prev;

	if (page->vnode_prev)
		page->vnode_prev->vnode_next = page->vnode_next;
	else
		page->backing->pages = page->vnode_next;

	page->vnode_next = NULL;
	page->vnode_prev = NULL;
	--mm_cache_cached_pages;
}

int mm_cache_get_page(vnode_t *vnode, uintmax_t offset, int flags, page_t **res) {
	(void) flags;
	__assert(vnode->type == V_TYPE_REGULAR || vnode->type == V_TYPE_BLKDEV);
	__assert((offset % PAGE_SIZE) == 0);
	retry_err:
	HOLD_LOCK();

	page_t *new_page = NULL;
	volatile page_t *page = find_page(vnode, offset);
	retry:
	if (page) {
		// page is present in the page cache
		mm_hold_page(mm_get_page_address((page_t *)page));
		RELEASE_LOCK();

		// in the case of a retry, release the allocated page here
		if (new_page)
			mm_release_page(mm_get_page_address(new_page));

		eventlistener_t listener;
		EVENT_INITLISTENER(&listener);
		EVENT_ATTACH(&listener, &page_ready_event);

		// wait for page to be ready
		while ((page->flags & (PAGE_FLAGS_READY | PAGE_FLAGS_ERROR)) == 0)
			EVENT_WAIT(&listener, 0);

		EVENT_DETACHALL(&listener);

		if (page->flags & PAGE_FLAGS_ERROR) {
			// the thread handling the page in failed to read it, we should retry it and see whats up
			mm_release_page(mm_get_page_address((page_t *)page));
			goto retry_err;
		}

		*res = (page_t *)page;
	} else {
		// page is not present in the cache, we will have to load it in
		RELEASE_LOCK();

		void *address = mm_alloc_page(MEMORY_SECTION_DEFAULT);
		if (address == NULL)
			return ENOMEM;

		new_page = mm_get_page(address);

		HOLD_LOCK();

		// while the lock wasn't being held, the page could have potentially been added to the cache
		// check for it again and return from the function as if it was always there in the first place
		page = find_page(vnode, offset);
		if (page)
			goto retry;

		new_page->backing = vnode;
		new_page->offset = offset;

		// add it to the page cache
		put_page(new_page);

		RELEASE_LOCK();

		VOP_LOCK(vnode);
		int error = VOP_GETPAGE(vnode, offset, new_page);
		VOP_UNLOCK(vnode);

		if (error) {
			// an error happened with GETPAGE, remove the page from the cache,
			// tell the sleeping threads that something happened and free the page
			// by setting backing to null so it gets treated as an anonymous page again
			HOLD_LOCK();
			remove_page(new_page);

			new_page->flags |= PAGE_FLAGS_ERROR;
			new_page->backing = NULL;
			new_page->offset = 0;

			RELEASE_LOCK();
			mm_release_page(mm_get_page_address(new_page));
			EVENT_SIGNAL(&page_ready_event);
			return error;
		}

		HOLD_LOCK();
		new_page->flags |= PAGE_FLAGS_READY;
		RELEASE_LOCK();

		EVENT_SIGNAL(&page_ready_event);
		*res = new_page;
	}

	return 0;
}

// adds a page to the cache in a specific offset if its not already there
int mm_cache_push_page(vnode_t *vnode, uintmax_t offset, page_t *page) {
	__assert((offset % PAGE_SIZE) == 0);
	HOLD_LOCK();

	page_t *page_test = find_page(vnode, offset);
	if (page_test) {
		RELEASE_LOCK();
		return EAGAIN;
	}

	page->backing = vnode;
	page->offset = offset;
	page->flags |= PAGE_FLAGS_READY;

	put_page(page);

	RELEASE_LOCK();
	return 0;
}

// removes a page from the cache AND turns it into anonymous memory
int mm_cache_evict(page_t *page) {
	HOLD_LOCK();
	if (page->refcount > 1) {
		RELEASE_LOCK();
		return EAGAIN;
	}
	__assert(page->refcount == 1);
	__assert((page->flags & PAGE_FLAGS_DIRTY) == 0);

	if ((page->flags & PAGE_FLAGS_TRUNCATED) == 0) {
		// the page needs to be removed from the cache to continue
		remove_page(page);
	}

	page->flags = 0;
	page->backing = NULL;
	page->offset = 0;

	RELEASE_LOCK();
	return 0;
}

// removes a page from the cache *but doesn't do anything to it*
int mm_cache_take_page(page_t *page) {
	HOLD_LOCK();
	// someone called mm_cache_get_page() and got this page while the lock wasn't held
	// return an error status to the caller
	if (page->refcount > 1) {
		RELEASE_LOCK();
		return EAGAIN;
	}

	__assert(page->refcount == 1);
	__assert((page->flags & PAGE_FLAGS_DIRTY) == 0);

	// XXX maybe just not allow truncated pages to take up space like this?
	if ((page->flags & PAGE_FLAGS_TRUNCATED) == 0) {
		// the page needs to be removed from the cache to continue
		remove_page(page);
	}

	RELEASE_LOCK();
	return 0;
}

int mm_cache_truncate(vnode_t *vnode, uintmax_t offset) {
	HOLD_LOCK();
	page_t *page_list = NULL;
	page_t *page = vnode->pages;

	while (page) {
		page_t *old_page = page;
		page = page->vnode_next;

		// only truncate past a certain offset
		if (old_page->offset < offset)
			continue;

		old_page->flags |= PAGE_FLAGS_TRUNCATED;
		remove_page(old_page);
		old_page->vnode_next = page_list;
		page_list = old_page;
	}

	RELEASE_LOCK();

	// make sure to unref if they are pinned
	while (page_list) {
		page_t *page = page_list;
		page_list = page_list->vnode_next;
		if (page->flags & PAGE_FLAGS_PINNED)
			mm_release_page(mm_get_page_address(page));
	}

	return 0;
}

// called with lock held
// returns with lock released
// expects backing lock to be held
static int sync_page(page_t *page, bool backing_lock) {
	__assert(page->flags & PAGE_FLAGS_DIRTY);
	page->flags &= ~PAGE_FLAGS_DIRTY;
	RELEASE_LOCK();
	int e = 0;
	if ((page->flags & PAGE_FLAGS_TRUNCATED) == 0) {
		if (backing_lock)
			VOP_LOCK(page->backing);

		e = VOP_PUTPAGE(page->backing, page->offset, (page_t *)page);

		if (backing_lock)
			VOP_UNLOCK(page->backing);
		VOP_RELEASE(page->backing);
	} else {
		// page got truncated from the file while waiting to be written to disk
		// its still holding a reference to the vnode, so release that
		VOP_RELEASE(page->backing);
	}

	mm_release_page(mm_get_page_address((page_t *)page));
	return e;
}

static page_t *dirty_list;
static page_t *dirty_list_end;

// expects vnode to be held
int mm_cache_sync_vnode(vnode_t *vnode, uintmax_t offset, size_t size) {
	offset = ROUND_DOWN(offset, PAGE_SIZE);
	uintmax_t top = offset + size;
	// overflow check
	__assert(top > offset);
	HOLD_LOCK();

	// loop through all vnode pages in memory and check which ones are in the range and are dirty
	// TODO create a proper vnode dirty list as to not have to loop through the ENTIRE thing in memory
	page_t *page = vnode->pages;
	page_t *vnode_dirty_list = NULL;
	for (; page; page = page->vnode_next) {
		if (page->offset < offset || page->offset >= top || (page->flags & PAGE_FLAGS_DIRTY) == 0 || (page->flags & PAGE_FLAGS_VNODE_SYNCING))
			continue;

		// remove from write list and add to an internal list using the write pointers
		// in a singly linked list way
		if (page->write_next)
			page->write_next->write_prev = page->write_prev;
		else
			dirty_list_end = page->write_prev;

		if (page->write_prev)
			page->write_prev->write_next = page->write_next;
		else
			dirty_list = page->write_next;

		page->write_next = vnode_dirty_list;
		page->write_prev = NULL;
		page->flags |= PAGE_FLAGS_VNODE_SYNCING;
		vnode_dirty_list = page;
	}

	RELEASE_LOCK();

	int e = 0;
	while (vnode_dirty_list) {
		// in the case of failure, only the first error to occur will be reported and we will not
		// retry the write and keep on syncing the pages to disk
		HOLD_LOCK();
		page_t *page = vnode_dirty_list;
		vnode_dirty_list = vnode_dirty_list->write_next;
		page->write_next = NULL;
		page->flags &= ~PAGE_FLAGS_VNODE_SYNCING;

		// another thread could already have synced this page, verify if it is still dirty
		if (page->flags & PAGE_FLAGS_DIRTY) {
			int error = sync_page(page, false);

			if (e == 0)
				e = error;
		} else {
			RELEASE_LOCK();
		}
		// sync_page returns with lock released
	}

	return e;
}

int mm_cache_sync(void) {
	eventlistener_t event_listener;
	EVENT_INITLISTENER(&event_listener);
	HOLD_LOCK();
	if (dirty_list == NULL) {
		// no dirty pages
		RELEASE_LOCK();
		return 0;
	}

	EVENT_ATTACH(&event_listener, &sync_event);
	semaphore_signal(&sync);
	RELEASE_LOCK();

	EVENT_WAIT(&event_listener, 0);

	EVENT_DETACHALL(&event_listener);
	return 0;
}

// backing expected locked
int mm_cache_make_dirty(page_t *page) {
	bool made_dirty = false;
	HOLD_LOCK();

	if ((page->flags & (PAGE_FLAGS_DIRTY | PAGE_FLAGS_TRUNCATED)) == 0) {
		made_dirty = true;
		// page is neither dirty nor truncated, add to dirty list and hold the page and vnode
		page->flags |= PAGE_FLAGS_DIRTY;

		if ((page->flags & PAGE_FLAGS_VNODE_SYNCING) == 0) {
			page->write_prev = NULL;
			page->write_next = dirty_list;
			if (dirty_list)
				dirty_list->write_prev = page;
			else
				dirty_list_end = page;

			dirty_list = page;
		}

		mm_hold_page(mm_get_page_address(page));
		__assert(page->backing);
		VOP_HOLD(page->backing);
	}

	RELEASE_LOCK();
	if (made_dirty) {
		vattr_t attr;
		attr.mtime = timekeeper_time();
		VOP_SETATTR(page->backing, &attr, V_ATTR_MTIME, NULL);
	}
	return 0;
}

static void tick(context_t *, dpcarg_t arg) {
	semaphore_signal(&sync);
}

static void writer() {
	timerentry_t timer_entry;
	// this will be inserted on some random cpu's timer, but it will always work after that
	interrupt_set(false);
	timer_insert(current_cpu()->timer, &timer_entry, tick, NULL, (uintmax_t)WRITER_TICK_SECONDS * 1000000, true);
	interrupt_set(true);
	for (;;) {
		HOLD_LOCK();
		volatile page_t *page = dirty_list_end;
		if (page == NULL) {
			EVENT_SIGNAL(&sync_event);
			RELEASE_LOCK();
			semaphore_wait(&sync, false);
			continue;
		}

		dirty_list_end = page->write_prev;
		if (dirty_list_end)
			dirty_list_end->write_next = NULL;
		else
			dirty_list = NULL;

		page->write_prev = NULL;
		// TODO notify error on mm_cache_sync_vnode
		sync_page((page_t *)page, true);
	}
}

void mm_cache_init(void) {
	MUTEX_INIT(&mutex);
	table = mm_map(NULL, TABLE_SIZE * sizeof(page_t *), MM_RANGE_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	__assert(table);
	memset(table, 0, TABLE_SIZE * sizeof(page_t *));

	SEMAPHORE_INIT(&sync, 0);
	writer_thread = sched_newthread(writer, PAGE_SIZE * 16, 1, NULL, NULL);
	__assert(writer_thread);
	sched_queue(writer_thread);
	mm_cache_sync();
	EVENT_INITHEADER(&sync_event);
	EVENT_INITHEADER(&page_ready_event);
}

INIT_ROUTINE_DEFINE(mm_cache, INIT_ROUTINE_FLAGS_NONE, mm_cache_init, scheduler);
