#include <kernel/mm.h>
#include <logging.h>
#include <kernel/page.h>
#include <util.h>
#include <arch/cpu.h>
#include <limine.h>
#include <string.h>
#include <kernel/slab.h>
#include <kernel/init.h>

// ranges are separated into kernel and user. the kernel has a temporary user context
mm_context_t mm_kernel_ctx;
static mm_space_t kernel_space = {
	.start = KERNELSPACE_START,
	.end = KERNELSPACE_END
};

// returns a pointer to the space of vaddr
static mm_space_t *get_space(void *vaddr) {
	if (USERSPACE_START <= vaddr && vaddr < USERSPACE_END)
		return &current_mm_context()->space;
	else if (KERNELSPACE_START <= vaddr && vaddr < KERNELSPACE_END)
		return &kernel_space;
	else
		return NULL;
}

static int mm_range_compare(rbtree_t *a, rbtree_t *b) {
	mm_range_t *range_a = container_of(a, mm_range_t, rbtree_node);
	mm_range_t *range_b = container_of(b, mm_range_t, rbtree_node);

	if (range_a->start == range_b->start)
		return 0;

	return range_a->start > range_b->start ? 1 : -1;
}

int mm_change_mmu_flags(void *base, size_t size, mmuflags_t mmuflags, int flags) {
	base = (void *)ROUND_DOWN((uintptr_t)base, PAGE_SIZE);

	if (flags & MM_RANGE_FLAGS_PAGESIZE)
		size *= PAGE_SIZE;
	else
		size = ROUND_UP(size, PAGE_SIZE);

	if (size == 0)
		return 0;

	mm_space_t *space = get_space(base);
	if (space == NULL)
		return ENOMEM;

	if (space == &kernel_space)
		mmuflags |= ARCH_MMU_FLAGS_GLOBAL;

	MUTEX_ACQUIRE(&space->lock);

	int error = mm_change_range(space, base, size, false, flags, mmuflags);
	arch_mmu_invalidate_range(base, size);

	MUTEX_RELEASE(&space->lock);
	return error;
}

static void print_space(mm_space_t *space) {
	printf("mm: ranges:\n");
	rbtree_t *rbtree = rbtree_first(space->ranges);
	while (rbtree) {
		mm_range_t *range = container_of(rbtree, mm_range_t, rbtree_node);
		printf("mm: address %p size %lx flags %x\n", range->start, range->size, range->flags);
		rbtree = rbtree_successor(rbtree);
	}
}

static void *zero_page;

// the range's space is expected to be locked
int mm_partial_page_in(mm_range_t *range, void *vaddr, page_t **resulting_page) {
	bool present = arch_mmu_ispresent(current_mm_context()->pagetable, vaddr);
	bool file_mapping = range->flags & MM_RANGE_FLAGS_FILE;
	bool cacheable = file_mapping && vfs_iscacheable(range->vnode);
	bool private = (range->flags & MM_RANGE_FLAGS_SHARED) == 0 || file_mapping == false; // TODO

	if (present) {
		void *mapped_address = arch_mmu_getphysical(current_mm_context()->pagetable, vaddr);
		*resulting_page = mm_get_page(mapped_address);
		return 0;
	}

	if (file_mapping) {
		size_t map_offset = (uintptr_t)vaddr - (uintptr_t)range->start;
		int error;

		if (cacheable) {
			page_t *res;
			error = mm_cache_get_page(range->vnode, range->offset + map_offset, 0, &res);
			if (error)
				return error;

			void *new_phys = mm_get_page_address(res);
			if (!arch_mmu_map(current_mm_context()->pagetable, new_phys, vaddr, range->mmuflags & ~ARCH_MMU_FLAGS_WRITE)) {
				mm_release_page(new_phys);
				error = ENOMEM;
			}

			*resulting_page = res;
		} else {
			cred_t *cred = current_thread()->proc ? &current_thread()->proc->cred : NULL;
			VOP_LOCK(range->vnode);
			error = VOP_MMAP(range->vnode, vaddr, range->offset + map_offset, mm_mmu_flags_to_vnode_flags(range->mmuflags) | (private ? 0 : V_FFLAGS_SHARED), cred);
			VOP_UNLOCK(range->vnode);
			*resulting_page = NULL; // we cannot guarantee that device mappings have a valid backing
		}

		return error;
	}

	// anonymous memory
	if (range->mmuflags & ARCH_MMU_FLAGS_WRITE) {
		void *new_page = mm_alloc_page(MEMORY_SECTION_DEFAULT);
		if (new_page == NULL)
			return ENOMEM;

		memset(MAKE_HHDM(new_page), 0, PAGE_SIZE);
		if (!arch_mmu_map(current_mm_context()->pagetable, new_page, vaddr, range->mmuflags)) {
			mm_release_page(new_page);
			return ENOMEM;
		}

		*resulting_page = mm_get_page(new_page);
		return 0;
	}

	int error = ENOMEM;
	if (arch_mmu_map(current_mm_context()->pagetable, zero_page, vaddr, range->mmuflags)) {
		mm_hold_page(zero_page);
		*resulting_page = mm_get_page(zero_page);
		error = 0;
	}

	return error;
}

// the range's space is expected to be locked
int mm_full_page_in(mm_range_t *range, void *vaddr, page_t **resulting_page) {
	void *mapped_address = arch_mmu_getphysical(current_mm_context()->pagetable, vaddr);
	bool present = arch_mmu_ispresent(current_mm_context()->pagetable, vaddr);
	bool writable = arch_mmu_iswritable(current_mm_context()->pagetable, vaddr);
	bool file_mapping = range->flags & MM_RANGE_FLAGS_FILE;
	bool cacheable = file_mapping && vfs_iscacheable(range->vnode);
	bool private = (range->flags & MM_RANGE_FLAGS_SHARED) == 0 || file_mapping == false; // TODO

	if (present && (!(range->mmuflags & ARCH_MMU_FLAGS_WRITE) || writable)) {
		// already fully paged in
		*resulting_page = mm_get_page(mapped_address);
		return 0;
	}

	if (present) {
		// partially paged in
		page_t *current_page = mm_get_page(mapped_address);

		if ((!private || (!file_mapping && current_page->refcount == 1)) && mapped_address != zero_page) {
			arch_mmu_remap(current_mm_context()->pagetable, mapped_address, vaddr, range->mmuflags);

			if (file_mapping && cacheable) {
				VOP_LOCK(range->vnode);
				mm_cache_make_dirty(current_page);
				VOP_UNLOCK(range->vnode);
			}

			*resulting_page = current_page;
			return 0;
		} else {
			void *new_phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
			if (new_phys == NULL)
				return ENOMEM;

			memcpy(MAKE_HHDM(new_phys), MAKE_HHDM(mapped_address), PAGE_SIZE);
			arch_mmu_remap(current_mm_context()->pagetable, new_phys, vaddr, range->mmuflags);
			arch_mmu_invalidate_range(vaddr, PAGE_SIZE);
			if ((range->flags & MM_RANGE_FLAGS_FILE) == 0 || cacheable)
				mm_release_page(mapped_address);

			*resulting_page = mm_get_page(new_phys);
			return 0;
		}
	}

	// completely empty PTE
	if (!file_mapping) {
		void *new_phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
		if (new_phys == NULL)
			return ENOMEM;

		memset(MAKE_HHDM(new_phys), 0, PAGE_SIZE);
		if (!arch_mmu_map(current_mm_context()->pagetable, new_phys, vaddr, range->mmuflags)) {
			mm_release_page(new_phys);
			return ENOMEM;
		}

		*resulting_page = mm_get_page(new_phys);
		return 0;
	}

	size_t map_offset = (uintptr_t)vaddr - (uintptr_t)range->start;

	if (!cacheable) {
		cred_t *cred = current_thread()->proc ? &current_thread()->proc->cred : NULL;
		VOP_LOCK(range->vnode);
		int error = VOP_MMAP(range->vnode, vaddr, range->offset + map_offset, mm_mmu_flags_to_vnode_flags(range->mmuflags) | (range->flags & MM_RANGE_FLAGS_SHARED ? V_FFLAGS_SHARED : 0), cred);
		VOP_UNLOCK(range->vnode);

		*resulting_page = NULL; // we cannot guarantee that device mappings have a valid backing
		return error;
	}

	page_t *vn_page;
	int error = mm_cache_get_page(range->vnode, range->offset + map_offset, 0, &vn_page);
	if (error)
		return error;

	void *vn_phys = mm_get_page_address(vn_page);

	if (!private) {
		if (!arch_mmu_map(current_mm_context()->pagetable, vn_phys, vaddr, range->mmuflags)) {
			mm_release_page(vn_phys);
			return ENOMEM;
		}

		if (cacheable && (range->mmuflags & ARCH_MMU_FLAGS_WRITE)) {
			VOP_LOCK(range->vnode);
			mm_cache_make_dirty(vn_page);
			VOP_UNLOCK(range->vnode);
		}

		*resulting_page = vn_page;
		return 0;
	}

	void *new_phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
	if (new_phys == NULL) {
		mm_release_page(vn_phys);
		return ENOMEM;
	}

	memcpy(MAKE_HHDM(new_phys), MAKE_HHDM(vn_phys), PAGE_SIZE);
	mm_release_page(vn_phys);

	if (!arch_mmu_map(current_mm_context()->pagetable, new_phys, vaddr, range->mmuflags)) {
		mm_release_page(new_phys);
		return ENOMEM;
	}

	*resulting_page = mm_get_page(new_phys);

	return 0;
}

bool mm_handle_page_fault(void *addr, bool user, int actions) {
	if (user == false && addr > USERSPACE_END) {
		printf("mm: kernel access\n");
		return false;
	}

	addr = (void *)ROUND_DOWN((uintptr_t)addr, PAGE_SIZE);

	mm_space_t *space = get_space(addr);

	if (space == NULL || (space == &kernel_space && user)) {
		printf("mm: no such space or space accessed is kernel\n");
		return false;
	}

	MUTEX_ACQUIRE(&space->lock);
	mm_range_t *range = mm_get_range(space, addr);

	bool status = false;

	if (range == NULL) {
		printf("mm: no range\n");
		goto cleanup;
	}

	// check if valid

	int invalid_actions = 0;

	if ((range->mmuflags & ARCH_MMU_FLAGS_READ) == 0)
		invalid_actions |= MM_FAULT_ACTION_READ;

	if ((range->mmuflags & ARCH_MMU_FLAGS_WRITE) == 0)
		invalid_actions |= MM_FAULT_ACTION_WRITE;

	if ((range->mmuflags & ARCH_MMU_FLAGS_NOEXEC))
		invalid_actions |= MM_FAULT_ACTION_EXEC;

	if (invalid_actions & actions) {
		printf("mm: bad action\n");
		goto cleanup;
	}

	page_t *p;
	int error;
	if (!arch_mmu_ispresent(current_mm_context()->pagetable, addr))
		error = mm_partial_page_in(range, addr, &p);
	else
		error = mm_full_page_in(range, addr, &p);

	if (error) {
		printf("mm: failed to do page-in: %s (sending SIGBUS)\n", strerror(error));
		signal_signalthread(current_thread(), SIGBUS, true);
	}
	status = true;

	cleanup:
	MUTEX_RELEASE(&space->lock);
	return status;
}

// space is expected to be locked on call.
static int lock_page(mm_space_t *space, void *vaddr, int hint) {
	mm_range_t *range = mm_get_range(space, vaddr);
	bool read_hint = hint & MM_GET_PHYSICAL_ADDRESS_FLAGS_LOCK_HINT_READ;
	bool write_hint = hint & MM_GET_PHYSICAL_ADDRESS_FLAGS_LOCK_HINT_WRITE;
	if (!range || (write_hint && !(range->mmuflags & ARCH_MMU_FLAGS_WRITE)) || (read_hint && !(range->mmuflags & ARCH_MMU_FLAGS_READ)))
		return EFAULT;

	// since we cannot guarantee the mapping of a character device actually points to physical memory,
	// we cannot reliably lock it. returning an error here also protects from trying to DMA into non-physical memory
	// like the framebuffer.
	// same thing applies to an abstract physical memory mapping.
	if (((range->flags & MM_RANGE_FLAGS_FILE) && (range->vnode->type == V_TYPE_CHDEV)) || (range->flags & MM_RANGE_FLAGS_PHYSICAL))
		return EFAULT;

	// we need to make sure the address is fully paged in before locking, as it needs
	// to affect the final state of the page
	// the only exception to this is if we know this operation will be read-only, which can safely operate on a partial page.
	page_t *page;
	int error;
	if (read_hint && !write_hint)
		error = mm_partial_page_in(range, vaddr, &page);
	else
		error = mm_full_page_in(range, vaddr, &page);

	if (error)
		return error;

	if (page == NULL)
		return EINVAL;

	__atomic_add_fetch(&page->lock_count, 1, __ATOMIC_SEQ_CST);

	return 0;
}

void *mm_get_physical_address(void *addr, int flags) {
	void *aligned_addr = (void *)ROUND_DOWN((uintptr_t)addr, PAGE_SIZE);

	mm_space_t *space = get_space(aligned_addr);
	if (space == NULL)
		return NULL;

	MUTEX_ACQUIRE(&space->lock);

	void *physical;
	if ((flags & MM_GET_PHYSICAL_ADDRESS_FLAGS_LOCK) && lock_page(space, aligned_addr, flags)) {
		physical = NULL;
		goto leave;
	}

	physical = arch_mmu_getphysical(current_mm_context()->pagetable, aligned_addr);

	if (flags & MM_GET_PHYSICAL_ADDRESS_FLAGS_HOLD)
		mm_hold_page(physical);

	leave:
	MUTEX_RELEASE(&space->lock);
	return physical ? physical + ((uintptr_t)addr - (uintptr_t)aligned_addr) : NULL;
}

void *mm_map(void *addr, volatile size_t size, int flags, mmuflags_t mmuflags, void *private) {
	if (addr == NULL)
		addr = KERNELSPACE_START;

	// XXX maps where addr is not page aligned can break
	addr = (void *)ROUND_DOWN((uintptr_t)addr, PAGE_SIZE);
	if (flags & MM_RANGE_FLAGS_PAGESIZE)
		size *= PAGE_SIZE;
	else
		size = ROUND_UP(size, PAGE_SIZE);

	if (size == 0)
		return NULL;

	mm_space_t *space = get_space(addr);
	if (space == NULL)
		return NULL;

	if (space == &kernel_space)
		mmuflags |= ARCH_MMU_FLAGS_GLOBAL;

	MUTEX_ACQUIRE(&space->lock);
	mm_range_t *range = NULL;

	void *start = mm_get_free_range(space, addr, size);
	void *ret_addr = NULL;
	if (((flags & MM_RANGE_FLAGS_EXACT) && start != addr) || (start == NULL && !(flags & MM_RANGE_FLAGS_REPLACE)))
		goto cleanup;

	range = mm_alloc_range();
	if (range == NULL)
		goto cleanup;

	if (flags & MM_RANGE_FLAGS_REPLACE) {
		__assert(addr);
		ret_addr = addr;
		range->start = addr;
		range->size = size;
		range->flags = MM_PERMANENT_RANGE_FLAGS_MASK & flags;
		range->mmuflags = mmuflags;
		__assert((flags & (MM_RANGE_FLAGS_ALLOCATE | MM_RANGE_FLAGS_PHYSICAL)) == 0);
		// make the memory inacessible
		mm_change_range(space, addr, size, false, flags, 0);
		arch_mmu_invalidate_range(addr, size);
		// and then free it
		mm_change_range(space, addr, size, true, flags, 0);
	} else {
		ret_addr = start;
		range->start = start;
		range->size = size;
		range->flags = MM_PERMANENT_RANGE_FLAGS_MASK & flags;
		range->mmuflags = mmuflags;
	}

	if (flags & MM_RANGE_FLAGS_FILE) {
		// XXX make sure that writes can't happen to executable memory mapped files and check file permissions
		mm_map_file_desc_t *desc = private;
		__assert((desc->offset % PAGE_SIZE) == 0);
		range->vnode = desc->node;
		range->offset = desc->offset;
		VOP_HOLD(desc->node);
	}

	if (flags & MM_RANGE_FLAGS_PHYSICAL) {
		// map to allocated virtual memory
		for (uintmax_t i = 0; i < size; i += PAGE_SIZE) {
			if (arch_mmu_map(current_mm_context()->pagetable, (void *)((uintptr_t)private + i), (void *)((uintptr_t)start + i), mmuflags) == false) {
				for (uintmax_t j = 0; j < size; j += PAGE_SIZE)
					arch_mmu_unmap(current_mm_context()->pagetable, (void *)((uintptr_t)start + j));

				// invalidate here just to be sure
				arch_mmu_invalidate_range(start, size);
				ret_addr = NULL;
				goto cleanup;
			}
		}
	} else if (flags & MM_RANGE_FLAGS_ALLOCATE) {
		// allocate to virtual memory
		for (uintmax_t i = 0; i < size; i += PAGE_SIZE) {
			void *allocated = mm_alloc_page(MEMORY_SECTION_DEFAULT);
			if (allocated == NULL) {
				ret_addr = NULL;
				goto cleanup;
			}

			if (arch_mmu_map(current_mm_context()->pagetable, allocated, (void *)((uintptr_t)start + i), mmuflags) == false) {
				for (uintmax_t j = 0; j < size; j += PAGE_SIZE) {
						void *virt = (void *)((uintptr_t)start + i);
						void *physical = arch_mmu_getphysical(current_mm_context()->pagetable, virt);

						if (physical) {
							mm_release_page(physical);
							arch_mmu_unmap(current_mm_context()->pagetable, virt);
						}
				}

				// invalidate here just to be sure
				arch_mmu_invalidate_range(start, size);
				ret_addr = NULL;
				goto cleanup;
			}
			memset(MAKE_HHDM(allocated), 0, PAGE_SIZE);
		}
	}

	mm_insert_range(space, range);
	cleanup:
	if (ret_addr == NULL && range)
		mm_free_range(range);

	MUTEX_RELEASE(&space->lock);
	return ret_addr;
}

void mm_unmap(void *addr, size_t size, int flags) {
	addr = (void *)ROUND_DOWN((uintptr_t)addr, PAGE_SIZE);

	if (flags & MM_RANGE_FLAGS_PAGESIZE)
		size *= PAGE_SIZE;
	else
		size = ROUND_UP(size, PAGE_SIZE);

	if (size == 0)
		return;

	mm_space_t *space = get_space(addr);
	if (space == NULL)
		return;

	MUTEX_ACQUIRE(&space->lock);

	// make memory inacessible
	mm_change_range(space, addr, size, false, flags, 0);
	arch_mmu_invalidate_range(addr, size);

	// and then free it
	mm_change_range(space, addr, size, true, flags, 0);

	MUTEX_RELEASE(&space->lock);
}

static scache_t *ctx_cache;

static bool ctx_ctor(scache_t *cache, void *obj) {
	mm_context_t *ctx = obj;
	ctx->space.start = USERSPACE_START;
	ctx->space.end = USERSPACE_END;
	MUTEX_INIT(&ctx->space.lock);
	ctx->space.ranges = NULL;
	return true;
}

mm_context_t *mm_create_context() {
	if (ctx_cache == NULL) {
		ctx_cache = slab_newcache(sizeof(mm_context_t), 0, ctx_ctor, NULL);
		__assert(ctx_cache);
	}

	mm_context_t *ctx = slab_allocate(ctx_cache);
	if (ctx == NULL)
		return NULL;

	ctx->pagetable = arch_mmu_newtable();
	if (ctx->pagetable == NULL) {
		slab_free(ctx_cache, ctx);
		return NULL;
	}

	return ctx;
}

void mm_destroy_context(mm_context_t *context) {
	mm_context_t *old_ctx = current_thread()->mmctx;
	mm_switch_context(context);
	mm_unmap(context->space.start, context->space.end - context->space.start, 0);
	mm_switch_context(old_ctx);
	arch_mmu_destroytable(context->pagetable);

	context->space.ranges = NULL;
	slab_free(ctx_cache, context);
}

mm_context_t *mm_fork_context(mm_context_t *old_context) {
	mm_context_t *new_context = mm_create_context();
	if (new_context == NULL)
		return NULL;

	MUTEX_ACQUIRE(&old_context->space.lock);

	rbtree_t *rbtree = old_context->space.ranges ? rbtree_first(old_context->space.ranges) : NULL;
	while (rbtree) {
		mm_range_t *range = container_of(rbtree, mm_range_t, rbtree_node);
		mm_range_t *new_range = mm_alloc_range();
		if (new_range == NULL)
			goto error;

		memcpy(new_range, range, sizeof(mm_range_t));

		rbtree_insert(&new_context->space.ranges, &new_range->rbtree_node, mm_range_compare);
		if (range->flags & MM_RANGE_FLAGS_FILE)
			VOP_HOLD(range->vnode);

		for (uintptr_t offset = 0; offset < new_range->size; offset += PAGE_SIZE) {
			void *vaddr = (void *)((uintptr_t)new_range->start + offset);
			void *phys = arch_mmu_getphysical(old_context->pagetable, vaddr);
			if (phys == NULL)
				continue;

			bool writeable = arch_mmu_iswritable(old_context->pagetable, vaddr);
			bool private = !(range->flags & MM_RANGE_FLAGS_SHARED);
			if ((range->flags & MM_RANGE_FLAGS_FILE) && range->vnode->type == V_TYPE_CHDEV && !private) {
				// TODO: implement fork semantics for device mappings properly.
				if (arch_mmu_map(new_context->pagetable, phys, vaddr, new_range->mmuflags) == false)
					goto error;
				continue;
			}

			page_t *page = mm_get_page(phys);
			if (mm_is_page_locked(page) && writeable && private) {
				// the page is a private locked page, eagerly copy it to ensure fork/futex correctness
				void *new_phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
				if (new_phys == NULL)
					goto error;

				if (arch_mmu_map(new_context->pagetable, new_phys, vaddr, new_range->mmuflags) == false) {
					mm_release_page(new_phys);
					goto error;
				}

				memcpy(MAKE_HHDM(new_phys), MAKE_HHDM(phys), PAGE_SIZE);
			} else {
				// go through the normal CoW path
				if (arch_mmu_map(new_context->pagetable, phys, vaddr, new_range->mmuflags & ~ARCH_MMU_FLAGS_WRITE) == false)
					goto error;

				mm_hold_page(phys);

				arch_mmu_remap(old_context->pagetable, phys, vaddr, new_range->mmuflags & ~ARCH_MMU_FLAGS_WRITE);
			}
		}

		rbtree = rbtree_successor(rbtree);
	}

	// TODO do only userspace invalidation as to not send ipi to all cores
	arch_mmu_invalidate_range(NULL, 0);

	MUTEX_RELEASE(&old_context->space.lock);
	return new_context;
	error:
	MUTEX_RELEASE(&old_context->space.lock);
	mm_destroy_context(new_context);
	return NULL;
}

void mm_switch_context(mm_context_t *ctx) {
	if (current_thread())
		current_thread()->mmctx = ctx;
	set_current_mm_context(ctx);
	arch_mmu_switch(ctx->pagetable);
}

extern void *_text_start;
extern void *_data_start;
extern void *_rodata_start;
extern void *_text_end;
extern void *_data_end;
extern void *_rodata_end;

extern volatile struct limine_memmap_request mm_page_limine_map;

void mm_init() {
	// set up initial state
	mm_range_init();
	MUTEX_INIT(&kernel_space.lock);

	mm_kernel_ctx.pagetable = arch_mmu_newtable();
	__assert(mm_kernel_ctx.pagetable);

	mm_kernel_ctx.space.start = USERSPACE_START;
	mm_kernel_ctx.space.end = USERSPACE_END;

	mm_switch_context(&mm_kernel_ctx);

	// map HHDM
	for (uint64_t i = 0; i < mm_page_limine_map.response->entry_count; ++i) {
		struct limine_memmap_entry *e = mm_page_limine_map.response->entries[i];

		if (e->type != LIMINE_MEMMAP_USABLE && e->type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE && e->type != LIMINE_MEMMAP_EXECUTABLE_AND_MODULES && e->type != LIMINE_MEMMAP_FRAMEBUFFER)
			continue;

		mmuflags_t mmuflags = ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_GLOBAL;
		if (e->type == LIMINE_MEMMAP_FRAMEBUFFER)
			mmuflags |= ARCH_MMU_FLAGS_WC;

		__assert(mm_map(MAKE_HHDM(e->base), e->length, MM_RANGE_FLAGS_EXACT, mmuflags, NULL));
	}

	// map kernel
	__assert(mm_map(&_text_start, (uintptr_t)&_text_end - (uintptr_t)&_text_start, MM_RANGE_FLAGS_EXACT, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_GLOBAL, NULL));
	__assert(mm_map(&_rodata_start, (uintptr_t)&_rodata_end - (uintptr_t)&_rodata_start, MM_RANGE_FLAGS_EXACT, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_GLOBAL, NULL));
	__assert(mm_map(&_data_start, (uintptr_t)&_data_end - (uintptr_t)&_data_start, MM_RANGE_FLAGS_EXACT, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_GLOBAL, NULL));

	// null page
	mm_map(MAKE_HHDM(NULL), PAGE_SIZE, MM_RANGE_FLAGS_EXACT, ARCH_MMU_FLAGS_NOEXEC, NULL);

	// zero page
	zero_page = mm_alloc_page(MEMORY_SECTION_DEFAULT);
	memset(MAKE_HHDM(zero_page), 0, PAGE_SIZE);

	print_space(&kernel_space);
}

INIT_ROUTINE_DEFINE(mm, INIT_ROUTINE_FLAGS_NONE, mm_init, mmu, slab_early);

void mm_apinit() {
	mm_switch_context(&mm_kernel_ctx);
}
