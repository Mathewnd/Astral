#include <kernel/mm.h>
#include <kernel/event.h>
#include <kernel/scheduler.h>
#include <kernel/timekeeper.h>
#include <arch/cpu.h>
#include <logging.h>
#include <mutex.h>
#include <list.h>
#include <util.h>

#define WRITER_TICK_SECONDS 15

static mutex_t vnode_dirty_list_mutex;
static list_t vnode_dirty_list;
static bool writer_syncing;
static eventheader_t writer_wakeup_event;
static eventheader_t sync_complete_event;

void mm_cache_make_dirty(vnode_t *vnode, page_t *page) {
	// check this here as a fast-path to reduce the mutex contention
	int flags = __atomic_load_n(&page->flags, __ATOMIC_ACQUIRE);
	if (flags & PAGE_FLAGS_TRUNCATED)
		return;

	MUTEX_ACQUIRE(&vnode->dirty_list_mutex);

	flags = __atomic_load_n(&page->flags, __ATOMIC_RELAXED);
	for (;;) {
		if (flags & PAGE_FLAGS_TRUNCATED) {
			MUTEX_RELEASE(&vnode->dirty_list_mutex);
			return;
		}

		int new_flags = flags | PAGE_FLAGS_DIRTY;
		if (__atomic_compare_exchange_n(&page->flags, &flags, new_flags, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED))
			break;
	}

	if (flags & (PAGE_FLAGS_DIRTY | PAGE_FLAGS_SYNCING)) {
		MUTEX_RELEASE(&vnode->dirty_list_mutex);
		return;
	}

	mm_hold_page(mm_get_page_address(page));
	list_push_back(&vnode->dirty_pages, &page->dirty_list_node);

	if (!vnode->dirty) {
		vnode->dirty = true;
		VOP_HOLD(vnode);

		MUTEX_ACQUIRE(&vnode_dirty_list_mutex);
		list_push_back(&vnode_dirty_list, &vnode->vnode_dirty_list_node);
		MUTEX_RELEASE(&vnode_dirty_list_mutex);
	}

	MUTEX_RELEASE(&vnode->dirty_list_mutex);

	vattr_t attr;
	attr.mtime = timekeeper_time();
	VOP_LOCK(vnode);
	VOP_SETATTR(vnode, &attr, V_ATTR_MTIME, NULL);
	VOP_UNLOCK(vnode);
}

// OK
static void mark_page_syncing(page_t *page) {
	int flags = __atomic_load_n(&page->flags, __ATOMIC_RELAXED);
	for (;;) {
		__assert(flags & PAGE_FLAGS_DIRTY);
		__assert((flags & PAGE_FLAGS_SYNCING) == 0);

		int new_flags = (flags & ~PAGE_FLAGS_DIRTY) | PAGE_FLAGS_SYNCING;
		if (__atomic_compare_exchange_n(&page->flags, &flags, new_flags, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
			break;
	}
}

// OK
// expects the per-vnode dirty list mutex to be held. returns true if the caller must release a vnode reference.
static bool try_undirty_vnode(vnode_t *vnode) {
	if (vnode->dirty == false)
		return false;

	MUTEX_ACQUIRE(&vnode_dirty_list_mutex);
	list_remove(&vnode_dirty_list, &vnode->vnode_dirty_list_node);
	MUTEX_RELEASE(&vnode_dirty_list_mutex);
	vnode->dirty = false;
	return true;
}

// OK
int mm_cache_sync_vnode(vnode_t *vnode) {
	int first_error = 0;
	MUTEX_ACQUIRE(&vnode->writeback_mutex);

	for (;;) {
		MUTEX_ACQUIRE(&vnode->dirty_list_mutex);
		list_node_t *page_node = list_pop_front(&vnode->dirty_pages);
		if (page_node == NULL) {
			bool was_dirty = try_undirty_vnode(vnode);
			MUTEX_RELEASE(&vnode->dirty_list_mutex);
			MUTEX_RELEASE(&vnode->writeback_mutex);

			if (was_dirty) {
				VOP_RELEASE(vnode);
			}
			return first_error;
		}

		page_t *page = container_of(page_node, page_t, dirty_list_node);
		mark_page_syncing(page);
		MUTEX_RELEASE(&vnode->dirty_list_mutex);

		VOP_LOCK(vnode);

		int error = 0;
		if ((__atomic_load_n(&page->flags, __ATOMIC_RELAXED) & PAGE_FLAGS_TRUNCATED) == 0)
			error = VOP_PUTPAGE(vnode, page->offset, page);

		VOP_UNLOCK(vnode);
		if (first_error == 0)
			first_error = error;
		if (error)
			__atomic_fetch_or(&page->flags, PAGE_FLAGS_DIRTY, __ATOMIC_RELEASE);

		int flags = __atomic_and_fetch(&page->flags, ~PAGE_FLAGS_SYNCING, __ATOMIC_ACQUIRE);
		if (flags & PAGE_FLAGS_DIRTY) {
			MUTEX_ACQUIRE(&vnode->dirty_list_mutex);
			list_push_back(&vnode->dirty_pages, &page->dirty_list_node);
			MUTEX_RELEASE(&vnode->dirty_list_mutex);
		} else {
			mm_release_page(mm_get_page_address(page));
		}

		if (error) {
			MUTEX_RELEASE(&vnode->writeback_mutex);
			return first_error;
		}
	}
}

// OK
int mm_cache_sync(void) {
	MUTEX_ACQUIRE(&vnode_dirty_list_mutex);

	if (vnode_dirty_list.head == NULL && !writer_syncing) {
		MUTEX_RELEASE(&vnode_dirty_list_mutex);
		return 0;
	}

	eventlistener_t listener;
	EVENT_INITLISTENER(&listener);
	EVENT_ATTACH(&listener, &sync_complete_event);

	EVENT_SIGNAL(&writer_wakeup_event);
	MUTEX_RELEASE(&vnode_dirty_list_mutex);

	EVENT_WAIT(&listener, 0);
	EVENT_DETACHALL(&listener);
	return 0;
}

// OK
static void writer_tick(context_t *, dpcarg_t) {
	EVENT_SIGNAL(&writer_wakeup_event);
}

// OK
static void writer(void) {
	long ipl = interrupt_raiseipl(IPL_DPC);
	timerentry_t timer_entry;
	timer_insert(current_cpu()->timer, &timer_entry, writer_tick, NULL, (uintmax_t)WRITER_TICK_SECONDS * 1000000, true);
	interrupt_loweripl(ipl);

	for (;;) {
		eventlistener_t listener;
		EVENT_INITLISTENER(&listener);

		MUTEX_ACQUIRE(&vnode_dirty_list_mutex);
		list_node_t *list_node = vnode_dirty_list.head;
		if (list_node == NULL) {
			writer_syncing = false;
			EVENT_ATTACH(&listener, &writer_wakeup_event);
			EVENT_SIGNAL(&sync_complete_event);
			MUTEX_RELEASE(&vnode_dirty_list_mutex);

			EVENT_WAIT(&listener, 0);
			EVENT_DETACHALL(&listener);
			continue;
		}

		vnode_t *vnode = container_of(list_node, vnode_t, vnode_dirty_list_node);
		VOP_HOLD(vnode);

		writer_syncing = true;
		MUTEX_RELEASE(&vnode_dirty_list_mutex);

		mm_cache_sync_vnode(vnode);

		VOP_RELEASE(vnode);
	}
}

// OK
void mm_cache_init_writer(void) {
	MUTEX_INIT(&vnode_dirty_list_mutex);
	list_init(&vnode_dirty_list);
	writer_syncing = false;
	EVENT_INITHEADER(&writer_wakeup_event);
	EVENT_INITHEADER(&sync_complete_event);

	thread_t *writer_thread = sched_newthread(writer, PAGE_SIZE * 2, 0, NULL, NULL);
	__assert(writer_thread);
	sched_queue(writer_thread);
}
