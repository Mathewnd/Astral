#include <kernel/abc.h>
#include <kernel/slab.h>
#include <kernel/init.h>
#include <kernel/scheduler.h>
#include <arch/cpu.h>
#include <logging.h>

#define WRITEBACK_MAX 32
#define WRITER_TICK_SECONDS 15

static scache_t *abc_block_cache;

void abc_release_block(abc_t *abc, abc_block_t *abc_block) {
	pushlock_acquire_exclusive(&abc->lock);
	if (__atomic_sub_fetch(&abc_block->refcount, 1, __ATOMIC_RELEASE) == 0) {
		__atomic_thread_fence(__ATOMIC_ACQUIRE);

		// another thread might have referenced the block while we were busy
		if (__atomic_load_n(&abc_block->refcount, __ATOMIC_RELAXED))
			goto leave;

		__assert(trie_remove(&abc->blocks, abc_block->block) == 0);

		mm_release_page(FROM_HHDM(abc_block->data));
		abc_block->block = 0;
		abc_block->refcount = 1;
		slab_free(abc_block_cache, abc_block);
	}
leave:
	pushlock_release_exclusive(&abc->lock);
}

static list_node_t *try_stealing_from_dirty(abc_t *abc, uint64_t block) {
	void *v;
	if (trie_lookup(&abc->blocks, block, &v))
		return NULL;

	abc_block_t *blk = v;
	if ((__atomic_load_n(&blk->flags, __ATOMIC_RELAXED) & ABC_BLOCK_FLAGS_DIRTY) == 0)
		return NULL;

	list_remove(&abc->dirty_list, &blk->dirty_list_node);
	__atomic_exchange_n(&blk->flags, ABC_BLOCK_FLAGS_BUSY, __ATOMIC_ACQUIRE);
	return &blk->dirty_list_node;
}

void abc_sync(abc_t *abc) {
	eventlistener_t listener;
	EVENT_INITLISTENER(&listener);
	EVENT_ATTACH(&listener, &abc->dirty_list_empty_event);

	pushlock_acquire_exclusive(&abc->dirty_list_lock);
	if (abc->dirty_list.head == NULL && !abc->syncing) {
		pushlock_release_exclusive(&abc->dirty_list_lock);
		EVENT_DETACHALL(&listener);
		return;
	}

	EVENT_SIGNAL(&abc->dirty_list_sync_event);
	pushlock_release_exclusive(&abc->dirty_list_lock);

	EVENT_WAIT(&listener, 0);
	EVENT_DETACHALL(&listener);
}

static void tick(context_t *, dpcarg_t arg) {
	abc_t *abc = arg;
	EVENT_SIGNAL(&abc->dirty_list_sync_event);
}

static void writer_thread(void) {
	abc_t *abc = current_thread()->kernelarg;

	long ipl = interrupt_raiseipl(IPL_DPC);
	timerentry_t timer_entry;
	timer_insert(current_cpu()->timer, &timer_entry, tick, abc, (uintmax_t)WRITER_TICK_SECONDS * 1000000, true);
	interrupt_loweripl(ipl);

	for (;;) {
		list_t internal_list;
		list_init(&internal_list);

		eventlistener_t listener;
		EVENT_INITLISTENER(&listener);
		EVENT_ATTACH(&listener, &abc->dirty_list_sync_event);

		pushlock_acquire_exclusive(&abc->dirty_list_lock);
		abc_block_t *abc_block = (abc_block_t *)list_pop_front(&abc->dirty_list);
		if (abc_block == NULL) {
			abc->syncing = false;
			EVENT_SIGNAL(&abc->dirty_list_empty_event);
			pushlock_release_exclusive(&abc->dirty_list_lock);
			EVENT_WAIT(&listener, 0);
			EVENT_DETACHALL(&listener);
			continue;
		}

		abc->syncing = true;
		EVENT_DETACHALL(&listener);

		list_push_front(&internal_list, &abc_block->dirty_list_node);
		__atomic_exchange_n(&abc_block->flags, ABC_BLOCK_FLAGS_BUSY, __ATOMIC_ACQUIRE);

		pushlock_acquire_shared(&abc->lock);
		size_t block_count = 1;
		if (abc_block->block) {
			uint64_t iterator = abc_block->block - 1;
			while (block_count < WRITEBACK_MAX) {
				list_node_t *node = try_stealing_from_dirty(abc, iterator);
				if (node == NULL)
					break;

				list_push_front(&internal_list, node);
				++block_count;
				if (iterator == 0)
					break;
				--iterator;
			}
		}

		uint64_t iterator = abc_block->block + 1;
		while (block_count < WRITEBACK_MAX) {
			list_node_t *node = try_stealing_from_dirty(abc, iterator);
			if (node == NULL)
				break;

			list_push_back(&internal_list, node);

			++block_count;
			++iterator;
		}

		pushlock_release_shared(&abc->lock);
		pushlock_release_exclusive(&abc->dirty_list_lock);

		abc_block_t *front_block = (abc_block_t *)internal_list.head;
		size_t blocks_per_page = PAGE_SIZE / abc->block_size;
		size_t iovec_count = (block_count + front_block->block % blocks_per_page + blocks_per_page - 1) / blocks_per_page;
		iovec_t iovec[iovec_count];
		abc_block_t *blk_it = front_block;

		size_t inserted = 0;
		size_t remaining_blocks = block_count;
		while (remaining_blocks) {
			size_t blocks_in_page = min(remaining_blocks, blocks_per_page - blk_it->block % blocks_per_page);
			iovec[inserted].addr = blk_it->data;
			iovec[inserted].len = blocks_in_page * abc->block_size;
			++inserted;
			for (size_t i = 0; i < blocks_in_page; ++i)
				blk_it = (abc_block_t *)blk_it->dirty_list_node.next;
			remaining_blocks -= blocks_in_page;
		}

		iovec_iterator_t iovec_iterator;
		iovec_iterator_init(&iovec_iterator, iovec, inserted);

		size_t written;
		int error = vfs_write_iovec(abc->backing, &iovec_iterator, block_count * abc->block_size, front_block->block * abc->block_size, &written, V_FFLAGS_NOCACHE);
		if (error) {
			// TODO: this should be propagated in some way for the filesystem
			// TODO: it should also set the blocks as dirty to retry
			printf("abc: writing back %lu blocks of filesystem metadata starting at block %lu failed. you should probably run fsck on your filesystems.\n", block_count, front_block->block);
		} else {
			__assert(written == block_count * abc->block_size);
		}

		list_for_each_safe(&internal_list, list_node) {
			abc_block_t *blk = (abc_block_t *)list_node;
			if (__atomic_and_fetch(&blk->flags, ~ABC_BLOCK_FLAGS_BUSY, __ATOMIC_ACQUIRE) & ABC_BLOCK_FLAGS_DIRTY) {
				pushlock_acquire_exclusive(&abc->dirty_list_lock);
				list_push_back(&abc->dirty_list, &blk->dirty_list_node);
				pushlock_release_exclusive(&abc->dirty_list_lock);
			} else {
				abc_release_block(abc, blk);
			}
		}
	}
}

// returns true if the thread should insert it into the dirty list
static bool try_to_set_dirty_flag(abc_block_t *abc_block) {
	int old_flags = __atomic_fetch_or(&abc_block->flags, ABC_BLOCK_FLAGS_DIRTY, __ATOMIC_RELEASE);
	return !(old_flags & ABC_BLOCK_FLAGS_DIRTY) && !(old_flags & ABC_BLOCK_FLAGS_BUSY);
}

void abc_make_dirty(abc_t *abc, abc_block_t *abc_block) {
	// TODO: fix this race
	pushlock_acquire_exclusive(&abc->dirty_list_lock);

	if (!try_to_set_dirty_flag(abc_block)) {
		pushlock_release_exclusive(&abc->dirty_list_lock);
		return;
	}

	__atomic_fetch_add(&abc_block->refcount, 1, __ATOMIC_RELAXED);

	list_push_back(&abc->dirty_list, &abc_block->dirty_list_node);
	pushlock_release_exclusive(&abc->dirty_list_lock);
}

static abc_block_t *get_block(abc_t *abc, uint64_t block) {
	void *val;
	int error = trie_lookup(&abc->blocks, block, &val);
	__assert(!error || error == ENOENT);
	if (error)
		return NULL;

	abc_block_t *abc_block = val;
	__atomic_fetch_add(&abc_block->refcount, 1, __ATOMIC_RELAXED);
	return abc_block;
}

int abc_get_block(abc_t *abc, uint64_t block, abc_block_t **ret) {
	pushlock_acquire_shared(&abc->lock);
	abc_block_t *abc_block = get_block(abc, block);
	pushlock_release_shared(&abc->lock);

	if (abc_block) {
		*ret = abc_block;
		return 0;
	}

	abc_block = slab_allocate(abc_block_cache);
	if (abc_block == NULL)
		return ENOMEM;

	trie_preallocation_t *trie_preallocation = trie_preallocate();
	if (trie_preallocation == NULL) {
		slab_free(abc_block_cache, abc_block);
		return ENOMEM;
	}

	page_t *page;
	int error = mm_cache_get_page(abc->backing, ROUND_DOWN(block * abc->block_size, PAGE_SIZE), &page);
	if (error) {
		slab_free(abc_block_cache, abc_block);
		trie_free_preallocation(trie_preallocation);
		return error;
	}
	size_t page_offset = block * abc->block_size - ROUND_DOWN(block * abc->block_size, PAGE_SIZE);

	pushlock_acquire_exclusive(&abc->lock);

	abc_block_t *abc_block_check = get_block(abc, block);
	if (abc_block_check) {
		pushlock_release_exclusive(&abc->lock);
		mm_release_page(mm_get_page_address(page));
		slab_free(abc_block_cache, abc_block);
		trie_free_preallocation(trie_preallocation);
		*ret = abc_block_check;
		return 0;
	}

	abc_block->block = block;
	abc_block->data = MAKE_HHDM((void *)((uintptr_t)mm_get_page_address(page) + page_offset));
	__assert(trie_insert_preallocated(&abc->blocks, block, abc_block, trie_preallocation) == 0);

	pushlock_release_exclusive(&abc->lock);

	trie_free_preallocation(trie_preallocation);

	*ret = abc_block;
	return 0;
}

int abc_init(abc_t *abc, vnode_t *vnode, size_t block_size) {
	__assert(block_size <= PAGE_SIZE);
	__assert(PAGE_SIZE % block_size == 0);
	abc->writer = sched_newthread(writer_thread, PAGE_SIZE * 2, 0, NULL, NULL);
	if (abc->writer == NULL)
		return ENOMEM;

	abc->lock = 0;
	trie_init(&abc->blocks);
	abc->block_size = block_size;

	abc->dirty_list_lock = 0;
	list_init(&abc->dirty_list);
	abc->syncing = false;
	EVENT_INITHEADER(&abc->dirty_list_sync_event);
	EVENT_INITHEADER(&abc->dirty_list_empty_event);

	VOP_HOLD(vnode);
	abc->backing = vnode;

	abc->writer->kernelarg = abc;
	sched_queue(abc->writer);

	return 0;
}

static bool abc_block_constructor(scache_t *, void *ptr) {
	abc_block_t *block = ptr;
	__atomic_store_n(&block->refcount, 1, __ATOMIC_RELAXED);
	__atomic_store_n(&block->flags, 0, __ATOMIC_RELAXED);
	return true;
}

static void abc_init_routine(void) {
	abc_block_cache = slab_newcache(sizeof(abc_block_t), 0, abc_block_constructor, NULL);
	__assert(abc_block_cache);
}

INIT_ROUTINE_DEFINE(abc, INIT_ROUTINE_FLAGS_NONE, abc_init_routine, scheduler, trie);
