#include <kernel/vmmcache.h>
#include <kernel/vmm.h>
#include <util.h>
#include <logging.h>

#define TABLE_SIZE 4096
#define WRITER_TICK_SECONDS 1

static mutex_t mutex;
static page_t **table;

static thread_t *writerthread;
static semaphore_t sync;

#define HOLD_LOCK() \
	MUTEX_ACQUIRE(&mutex, false);

#define RELEASE_LOCK() \
	MUTEX_RELEASE(&mutex);

static inline uint64_t fnv1ahash(void *buffer, size_t size);
static uintmax_t getentry(vnode_t *vnode, uintmax_t offset) {
	struct {
		vnode_t *vnode;
		uintmax_t offset;
	} tmp;

	tmp.vnode = vnode;
	tmp.offset = offset;

	return fnv1ahash(&tmp, sizeof(tmp)) % TABLE_SIZE;
}

// assumes lock is held
static page_t *findpage(vnode_t *vnode, uintmax_t offset) {
	uintmax_t entry = getentry(vnode, offset);

	page_t *page = table[entry];
	while (page) {
		if (page->backing == vnode && page->offset == offset)
			break;
		page = page->hashnext;
	}

	return page;
}

// assumes lock is held
static void putpage(page_t *page) {
	uintmax_t entry = getentry(page->backing, page->offset);
	// add to table list
	page->hashprev = NULL;
	page->hashnext = table[entry];
	if (table[entry])
		table[entry]->hashprev = page;

	table[entry] = page;

	// add to vnode list
	// XXX this could be a bit slow if a large portion of the file is in memory and it gets purged
	page->vnodeprev = NULL;
	page->vnodenext = page->backing->pages;
	if (page->backing->pages)
		page->backing->pages->vnodeprev = page;

	page->backing->pages = page;
}

// assumes lock is held
static void removepage(page_t *page) {
	uintmax_t entry = getentry(page->backing, page->offset);

	// remove from table list
	if (page->hashnext)
		page->hashnext->hashprev = page->hashprev;

	if (page->hashprev)
		page->hashprev->hashnext = page->hashnext;
	else
		table[entry] = page->hashnext;

	page->hashnext = NULL;
	page->hashprev = NULL;

	// remove from vnode list
	if (page->vnodenext)
		page->vnodenext->vnodeprev = page->vnodeprev;

	if (page->vnodeprev)
		page->vnodeprev->vnodenext = page->vnodenext;
	else
		page->backing->pages = page->vnodenext;

	page->vnodenext = NULL;
	page->vnodeprev = NULL;
}

int vmmcache_getpage(vnode_t *vnode, uintmax_t offset, page_t **res) {
	__assert(vnode->type == V_TYPE_REGULAR || vnode->type == V_TYPE_BLKDEV);
	__assert((offset % PAGE_SIZE) == 0);
	retry_err:
	HOLD_LOCK();

	page_t *newpage = NULL;
	volatile page_t *page = findpage(vnode, offset);
	retry:
	if (page) {
		// page is present in the page cache
		pmm_hold(pmm_getpageaddress((page_t *)page));
		RELEASE_LOCK();

		// in the case of a retry, release the allocated page here
		if (newpage)
			pmm_release(newpage);

		// wait for page to be ready
		while ((page->flags & (PAGE_FLAGS_READY | PAGE_FLAGS_ERROR)) == 0)
			sched_yield(); // TODO proper sleeping mechanism here

		if (page->flags & PAGE_FLAGS_ERROR) {
			// the thread handling the page in failed to read it, we should retry it and see whats up
			pmm_release((page_t *)page);
			goto retry_err;
		}

		*res = (page_t *)page;
	} else {
		// page is not present in the cache, we will have to load it in
		RELEASE_LOCK();

		void *address = pmm_allocpage(PMM_SECTION_DEFAULT);
		if (address == NULL)
			return ENOMEM;

		newpage = pmm_getpage(address);
		
		HOLD_LOCK();

		// while the lock wasn't being held, the page could have potentially been added to the cache
		// check for it again and return from the function as if it was always there in the first place
		page = findpage(vnode, offset);
		if (page)
			goto retry;

		newpage->backing = vnode;
		newpage->offset = offset;

		// add it to the page cache
		putpage(newpage);

		RELEASE_LOCK();

		int error = VOP_GETPAGE(vnode, offset, newpage);
		// TODO wake up the sleeping threads when implemented
		if (error) {
			// an error happened with GETPAGE, remove the page from the cache,
			// tell the sleeping threads that something happened and free the page
			// by setting backing to null so it gets treated as an anonymous page again
			HOLD_LOCK();
			removepage(newpage);

			newpage->flags |= PAGE_FLAGS_ERROR;
			newpage->backing = NULL;
			newpage->offset = 0;

			RELEASE_LOCK();
			pmm_release(newpage);
			return error;
		}

		newpage->flags |= PAGE_FLAGS_READY;
		*res = newpage;
	}

	RELEASE_LOCK();
	return 0;
}

int vmmcache_takepage(page_t *page) {
	HOLD_LOCK();
	// someone called vmmcache_getpage() and got this page while the lock wasn't held
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
		removepage(page);
	}

	RELEASE_LOCK();
	return 0;
}

int vmmcache_truncate(vnode_t *vnode, uintmax_t offset) {
	HOLD_LOCK();
	page_t *pagelist = NULL;
	page_t *page = vnode->pages;

	while (page) {
		page_t *oldpage = page;
		page = page->vnodenext;

		// only truncate past a certain offset
		if (oldpage->offset < offset)
			continue;

		oldpage->flags |= PAGE_FLAGS_TRUNCATED;
		removepage(oldpage);
		oldpage->vnodenext = pagelist;
		pagelist = oldpage;
	}

	RELEASE_LOCK();

	// make sure to unref if they are pinned
	while (pagelist) {
		page_t *page = pagelist;
		pagelist = pagelist->vnodenext;
		if (page->flags & PAGE_FLAGS_PINNED)
			pmm_release(pmm_getpageaddress(page));
	}

	return 0;
}

int vmmcache_syncvnode(vnode_t *vnode) {
	__assert(!"unimplemented");
}

int vmmcache_sync() {
	semaphore_signal(&sync);
	return 0;
}

static page_t *dirtylist;

int vmmcache_makedirty(page_t *page) {
	HOLD_LOCK();

	if ((page->flags & (PAGE_FLAGS_DIRTY | PAGE_FLAGS_TRUNCATED)) == 0) {
		// page is neither dirty nor truncated, add to dirty list and hold the page and vnode
		page->flags |= PAGE_FLAGS_DIRTY;

		page->writeprev = NULL;
		page->writenext = dirtylist;
		if (dirtylist)
			dirtylist->writeprev = page;

		dirtylist = page;
		pmm_hold(pmm_getpageaddress(page));
		__assert(page->backing);
		VOP_HOLD(page->backing);
	}

	RELEASE_LOCK();
	return 0;
}

static void tick(context_t *, dpcarg_t arg) {
	vmmcache_sync();
}

static void writer() {
	timerentry_t timerentry;
	// this will be inserted on some random cpu's timer, but it will always work after that
	interrupt_set(false);
	timer_insert(_cpu()->timer, &timerentry, tick, NULL, (uintmax_t)WRITER_TICK_SECONDS * 1000000, true);
	interrupt_set(true);
	for (;;) {
		HOLD_LOCK();
		// TODO make FIFO rather than LIFO
		volatile page_t *page = dirtylist;
		if (page == NULL) {
			RELEASE_LOCK();
			semaphore_wait(&sync, false);
			continue;
		}

		__assert(page->flags & PAGE_FLAGS_DIRTY);
		page->flags &= ~PAGE_FLAGS_DIRTY;

		dirtylist = page->writenext;
		if (dirtylist)
			dirtylist->writeprev = NULL;

		page->writenext = NULL;

		RELEASE_LOCK();
		if ((page->flags & PAGE_FLAGS_TRUNCATED) == 0) {
			VOP_PUTPAGE(page->backing, page->offset, (page_t *)page);
			VOP_RELEASE(page->backing);
		}

		pmm_release(pmm_getpageaddress((page_t *)page));
	}
}

void vmmcache_init() {
	MUTEX_INIT(&mutex);
	table = vmm_map(NULL, TABLE_SIZE * sizeof(page_t *), VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	__assert(table);
	memset(table, 0, TABLE_SIZE * sizeof(page_t *));

	SEMAPHORE_INIT(&sync, 0);
	writerthread = sched_newthread(writer, PAGE_SIZE * 4, 1, NULL, NULL);
	__assert(writerthread);
	sched_queue(writerthread);
	vmmcache_sync();
}
