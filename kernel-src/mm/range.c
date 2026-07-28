#include <kernel/mm.h>
#include <logging.h>
#include <kernel/page.h>
#include <util.h>
#include <arch/cpu.h>
#include <kernel/slab.h>

#define RANGE_TOP(x) (void *)((uintptr_t)x->start + x->size)

static scache_t *range_cache;

void mm_range_init(void) {
	range_cache = slab_create_new_cache_from_pmm(sizeof(mm_range_t), 0, NULL, NULL);
	__assert(range_cache);
}

mm_range_t *mm_alloc_range(void) {
	return slab_allocate(range_cache);
}

void mm_free_range(mm_range_t *range) {
	slab_free(range_cache, range);
}

static int rbtree_value_compare(void *addr, rbtree_t *node) {
	mm_range_t *range = container_of(node, mm_range_t, rbtree_node);
	void *range_top = RANGE_TOP(range);

	if (addr >= range_top)
		return 1;

	if (addr < range->start)
		return -1;

	return 0;
}

// get a range from an address
mm_range_t *mm_get_range(mm_space_t *space, void *addr) {
	rbtree_t *rbtree = rbtree_lookup(space->ranges, addr, rbtree_value_compare);
	return container_of(rbtree, mm_range_t, rbtree_node);
}

// get start of range that fits specific size from specific offset
void *mm_get_free_range(mm_space_t *space, void *addr, size_t size) {
	rbtree_t *rbtree = space->ranges;
	if (addr == NULL)
		addr = space->start;

	// no ranges
	if (rbtree == NULL)
		return addr;

	rbtree = rbtree_first(rbtree);
	mm_range_t *range = container_of(rbtree, mm_range_t, rbtree_node);

	// if theres free space before the first range
	if (range->start != space->start && addr < range->start && (uintptr_t)range->start - (uintptr_t)addr >= size)
		return addr;

	// check for space between two ranges
	rbtree_t *next = rbtree_successor(rbtree);
	while (next) {
		mm_range_t *next_range = container_of(next, mm_range_t, rbtree_node);

		void *range_top = RANGE_TOP(range);
		if (addr < range_top)
			addr = range_top;

		if (addr < next_range->start) {
			size_t free_size = (uintptr_t)next_range->start - (uintptr_t)addr;
			if (free_size >= size)
				return addr;
		}

		rbtree = next;
		range = container_of(rbtree, mm_range_t, rbtree_node);

		next = rbtree_successor(next);
	}

	// if theres free space after the last range
	void *range_top = RANGE_TOP(range);
	if (addr < range_top)
		addr = range_top;

	if (addr != space->end && (uintptr_t)space->end - (uintptr_t)addr >= size)
		return addr;

	return NULL;
}

static int rbtree_compare(rbtree_t *a, rbtree_t *b) {
	mm_range_t *range_a = container_of(a, mm_range_t, rbtree_node);
	mm_range_t *range_b = container_of(b, mm_range_t, rbtree_node);

	if (range_a->start == range_b->start)
		return 0;

	return range_a->start > range_b->start ? 1 : -1;
}

static bool ranges_compatible(mm_range_t *prev, mm_range_t *next) {
	void *prev_range_top = RANGE_TOP(prev);

	return prev_range_top == next->start && prev->flags == next->flags && prev->mmuflags == next->mmuflags && // general compatibility
		((prev->flags & MM_RANGE_FLAGS_FILE) == 0 || (prev->vnode == next->vnode && prev->offset + prev->size == next->offset)); // same file mapping, if any
}

void mm_insert_range(mm_space_t *space, mm_range_t *new_range) {
	rbtree_insert(&space->ranges, &new_range->rbtree_node, rbtree_compare);

	rbtree_t *successor = rbtree_successor(&new_range->rbtree_node);
	rbtree_t *predecessor = rbtree_predecessor(&new_range->rbtree_node);

	mm_range_t *next_range = successor ? container_of(successor, mm_range_t, rbtree_node) : NULL;
	mm_range_t *prev_range = predecessor ? container_of(predecessor, mm_range_t, rbtree_node) : NULL;

	// fragmentation checking

	// check next range
	if (next_range && ranges_compatible(new_range, next_range)) {
		new_range->size += next_range->size;

		rbtree_remove(&space->ranges, &next_range->rbtree_node);
		mm_free_range(next_range);
		if (new_range->flags & MM_RANGE_FLAGS_FILE) {
			VOP_RELEASE(new_range->vnode);
		}
	}

	// check prev range
	if (prev_range && ranges_compatible(prev_range, new_range)) {
		prev_range->size += new_range->size;

		rbtree_remove(&space->ranges, &new_range->rbtree_node);
		mm_free_range(new_range);
		if (prev_range->flags & MM_RANGE_FLAGS_FILE) {
			VOP_RELEASE(prev_range->vnode);
		}
	}
}

void mm_destroy_range(mm_range_t *range, uintmax_t _offset, size_t size, int flags) {
	// TODO find first
	uintmax_t top = _offset + size;

	for (uintmax_t offset = _offset; offset < top; offset += PAGE_SIZE) {
		void *vaddr = (void *)((uintptr_t)range->start + offset);
		void *physical = arch_mmu_getphysical(current_mm_context()->pagetable, vaddr);
		if (physical == NULL)
			continue;

		thread_t *thread = current_thread();
		proc_t *proc = thread ? thread->proc : NULL;
		cred_t *cred = proc ? &proc->cred : NULL;

		if ((range->flags & MM_RANGE_FLAGS_FILE) && ((range->flags & MM_RANGE_FLAGS_SHARED) || vfs_iscacheable(range->vnode) == false)) {
			// shared file mapping or non cacheable mapping
			if (vfs_iscacheable(range->vnode) == false) {
				// non cacheable mapping
				VOP_LOCK(range->vnode);
				__assert(VOP_MUNMAP(range->vnode, vaddr, range->offset + offset, mm_mmu_flags_to_vnode_flags(range->mmuflags) | (range->flags & MM_RANGE_FLAGS_SHARED ? V_FFLAGS_SHARED : 0), cred) == 0);
				VOP_UNLOCK(range->vnode);
			} else if (arch_mmu_iswritable(current_mm_context()->pagetable, vaddr)) {
				// dirty page cache mapping
				arch_mmu_unmap(current_mm_context()->pagetable, vaddr);
				mm_cache_make_dirty(mm_get_page(physical));
				mm_release_page(physical);
			} else {
				// non dirty page mapping
				arch_mmu_unmap(current_mm_context()->pagetable, vaddr);
				mm_release_page(physical);
			}
		} else {
			// anonymous, physical or private non character device mapping
			arch_mmu_unmap(current_mm_context()->pagetable, vaddr);
			if ((range->flags & MM_RANGE_FLAGS_PHYSICAL) == 0)
				mm_release_page(physical);
		}
	}

	if ((range->flags & MM_RANGE_FLAGS_FILE) && range->size == size)
		VOP_RELEASE(range->vnode);
}

#define CHANGE_MASK_CHECK(m, f, c, n) \
	if (((n) & (f)) == 0 && ((c) & (f))) \
			m |= f;

static void change_mmu_range(mm_range_t *range, void *base, size_t size, mmuflags_t new_flags) {
	for (uintmax_t offset = 0; offset < size; offset += PAGE_SIZE) {
		void *address = (void *)((uintptr_t)base + offset);

		mmuflags_t current_flags;
		// if page is not mapped, do nothing
		if (arch_mmu_getflags(current_mm_context()->pagetable, address, &current_flags) == false)
			continue;

		void *physical = arch_mmu_getphysical(current_mm_context()->pagetable, address);

		uintmax_t mask = 0;
		// check which flags are currently set and will be unset
		CHANGE_MASK_CHECK(mask, ARCH_MMU_FLAGS_READ, current_flags, new_flags);
		CHANGE_MASK_CHECK(mask, ARCH_MMU_FLAGS_WRITE, current_flags, new_flags);
		CHANGE_MASK_CHECK(mask, ARCH_MMU_FLAGS_USER, current_flags, new_flags);
		CHANGE_MASK_CHECK(mask, ARCH_MMU_FLAGS_NOEXEC, current_flags, new_flags);

		if ((range->flags & MM_RANGE_FLAGS_FILE) && (range->flags & MM_RANGE_FLAGS_SHARED) && (mask & ARCH_MMU_FLAGS_WRITE) && vfs_iscacheable(range->vnode)) {
			// removing write permissions from a writeable dirty shared mapped page, mark it as dirty, as
			// it won't be marked dirty upon a mm_unmap after this
			page_t *page = mm_get_page(physical);
			mm_cache_make_dirty(page);
		}

		// we will only change the mapping if the permissions decreased
		if (mask) {
			arch_mmu_remap(current_mm_context()->pagetable, physical, address, current_flags & ~mask);
		}
	}
}

static inline bool can_write_vnode(mm_range_t *range) {
	VOP_LOCK(range->vnode);
	int error = VOP_ACCESS(range->vnode, V_ACCESS_WRITE, &current_thread()->proc->cred);
	VOP_UNLOCK(range->vnode);
	return error == 0;
}

int mm_change_range(mm_space_t *space, void *address, size_t size, bool free, int flags, mmuflags_t new_mmuflags) {
	void *top = (void *)((uintptr_t)address + size);
	mm_range_t *new_range = NULL;

	if (space->ranges == NULL)
		return 0;

	// allocated here and as soon as its used to make sure that
	// even in an allocation failure there will always be a valid mapping
	if (free == false) {
		new_range = mm_alloc_range();
		if (new_range == NULL)
			return ENOMEM;
	}

	int error = 0;

	// get first range after address
	rbtree_t *rbtree = rbtree_find_first_larger_equal(space->ranges, address, rbtree_value_compare);
	mm_range_t *range = container_of(rbtree, mm_range_t, rbtree_node);

	if (rbtree == NULL)
		goto leave;

	// check if we actually start at the predecessor
	rbtree_t *predecessor = rbtree_predecessor(rbtree);
	if (predecessor) {
		mm_range_t *predecessor_range = container_of(predecessor, mm_range_t, rbtree_node);
		if (RANGE_TOP(predecessor_range) > address) {
			rbtree = predecessor;
			range = predecessor_range;
		}
	}

	// rbtree now has the first mm range to be unmapped/remapped

	// is the range we want to change in the middle of a mapping?
	if (address > range->start && top < RANGE_TOP(range)) {
		// split the mapping

		// check for write permission if changing a shared file mapping
		if (free == false && (flags & MM_RANGE_FLAGS_CREDCHECK) && (range->flags & MM_RANGE_FLAGS_SHARED) &&
			(range->flags & MM_RANGE_FLAGS_FILE) && can_write_vnode(range) == false) {
			error = EACCES;
			goto leave;
		}

		mm_range_t *split_range = mm_alloc_range();
		if (range == NULL) {
			error = ENOMEM;
			goto leave;
		}

		*split_range = *range; // copy most metadata

		if (free) {
			// release page data
			mm_destroy_range(range, (uintptr_t)address - (uintptr_t)range->start, size, 0);
		}

		// set up ranges
		split_range->start = top;
		split_range->size = (uintptr_t)RANGE_TOP(range) - (uintptr_t)split_range->start;
		range->size = (uintptr_t)address - (uintptr_t)range->start;

		if (range->flags & MM_RANGE_FLAGS_FILE) {
			VOP_HOLD(range->vnode);
			split_range->offset += range->size + size;
		}

		mm_insert_range(space, split_range);

		// if we are not just freeing memory, insert a new range
		if (free == false) {
			new_range->start = address;
			new_range->size = size;
			new_range->flags = range->flags;
			new_range->mmuflags = new_mmuflags;

			change_mmu_range(range, new_range->start, new_range->size, new_range->mmuflags);

			if (range->flags & MM_RANGE_FLAGS_FILE) {
				new_range->vnode = range->vnode;
				new_range->offset = range->offset + range->size;
				VOP_HOLD(range->vnode);
			}

			mm_insert_range(space, new_range);
			return 0; // dont free the new range
		}

		goto leave;
	}

	// are we starting from the end of a mapping?
	if (address > range->start) {
		// shrink the mapping

		// permission checking
		if (free == false && (flags & MM_RANGE_FLAGS_CREDCHECK) && (range->flags & MM_RANGE_FLAGS_SHARED) &&
			(range->flags & MM_RANGE_FLAGS_FILE) && can_write_vnode(range) == false) {
			error = EACCES;
			goto leave;
		}

		size_t difference = (uintptr_t)RANGE_TOP(range) - (uintptr_t)address;
		range->size -= difference;

		if (free) {
			mm_destroy_range(range, range->size, difference, 0);
			rbtree = rbtree_successor(rbtree);
		} else {
			// create a new range for the changes
			new_range->start = (void *)((uintptr_t)range->start + range->size);
			new_range->size = difference;
			new_range->flags = range->flags;
			new_range->mmuflags = new_mmuflags;

			change_mmu_range(range, new_range->start, new_range->size, new_range->mmuflags);

			if (range->flags & MM_RANGE_FLAGS_FILE) {
				new_range->vnode = range->vnode;
				new_range->offset = range->offset + range->size;
				VOP_HOLD(range->vnode);
			}

			mm_insert_range(space, new_range);

			rbtree = rbtree_successor(&new_range->rbtree_node);

			new_range = mm_alloc_range();
			if (new_range == NULL) {
				error = ENOMEM;
				goto leave;
			}
		}
	}

	// mappings that lie entirely in the range
	for (;;) {
		if (rbtree == NULL)
			goto leave;

		range = container_of(rbtree, mm_range_t, rbtree_node);
		if (RANGE_TOP(range) > top)
			break;

		rbtree = rbtree_successor(rbtree);

		// remove or change the flags of the range
		if (free) {
			rbtree_remove(&space->ranges, &range->rbtree_node);
			mm_destroy_range(range, 0, range->size, 0);
			mm_free_range(range);
		} else {
			// permission check
			if ((flags & MM_RANGE_FLAGS_CREDCHECK) && (range->flags & MM_RANGE_FLAGS_SHARED) &&
				(range->flags & MM_RANGE_FLAGS_FILE) && can_write_vnode(range) == false) {
				error = EACCES;
				goto leave;
			}

			change_mmu_range(range, range->start, range->size, new_mmuflags);
			range->mmuflags = new_mmuflags;
		}
	}

	// start of mapping is in the range
	if (range->start < top) {
		// move the start of the range

		// permission check
		if (free == false && (flags & MM_RANGE_FLAGS_CREDCHECK) && (range->flags & MM_RANGE_FLAGS_SHARED) &&
			(range->flags & MM_RANGE_FLAGS_FILE) && can_write_vnode(range) == false) {
			error = EACCES;
			goto leave;
		}

		size_t difference = (uintptr_t)top - (uintptr_t)range->start;
		if (free)
			mm_destroy_range(range, 0, difference, 0);

		range->start = (void *)((uintptr_t)range->start + difference);
		range->size -= difference;

		if (range->flags & MM_RANGE_FLAGS_FILE)
			range->offset += difference;

		if (free == false) {
			// insert range at the start
			new_range->start = (void *)((uintptr_t)range->start - difference);
			new_range->size = difference;
			new_range->flags = range->flags;
			new_range->mmuflags = new_mmuflags;

			change_mmu_range(range, new_range->start, new_range->size, new_range->mmuflags);

			if (range->flags & MM_RANGE_FLAGS_FILE) {
				new_range->vnode = range->vnode;
				new_range->offset = range->offset - difference;
				VOP_HOLD(range->vnode);
			}

			mm_insert_range(space, new_range);
			return 0; // don't free the range, just leave
		}
	}

	leave:
	if (free == false)
		mm_free_range(new_range);

	return error;
}
