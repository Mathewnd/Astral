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
static mm_space_t kernelspace = {
	.start = KERNELSPACE_START,
	.end = KERNELSPACE_END
};

// returns a pointer to the space of vaddr
static mm_space_t *getspace(void *vaddr) {
	if (USERSPACE_START <= vaddr && vaddr < USERSPACE_END)
		return &current_mm_context()->space;
	else if (KERNELSPACE_START <= vaddr && vaddr < KERNELSPACE_END)
		return &kernelspace;
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

	mm_space_t *space = getspace(base);
	if (space == NULL)
		return ENOMEM;

	if (space == &kernelspace)
		mmuflags |= ARCH_MMU_FLAGS_GLOBAL;

	MUTEX_ACQUIRE(&space->lock);

	int error = mm_change_range(space, base, size, false, flags, mmuflags);
	arch_mmu_invalidate_range(base, size);

	MUTEX_RELEASE(&space->lock);
	return error;
}

static void printspace(mm_space_t *space) {
	printf("mm: ranges:\n");
	rbtree_t *rbtree = rbtree_first(space->ranges);
	while (rbtree) {
		mm_range_t *range = container_of(rbtree, mm_range_t, rbtree_node);
		printf("mm: address %p size %lx flags %x\n", range->start, range->size, range->flags);
		rbtree = rbtree_successor(rbtree);
	}
}

static void *zeropage;

bool mm_handle_page_fault(void *addr, bool user, int actions) {
	if (user == false && addr > USERSPACE_END) {
		printf("mm: kernel access\n");
		return false;
	}

	addr = (void *)ROUND_DOWN((uintptr_t)addr, PAGE_SIZE);

	mm_space_t *space = getspace(addr);

	if (space == NULL || (space == &kernelspace && user)) {
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

	int invalidactions = 0;

	if ((range->mmuflags & ARCH_MMU_FLAGS_READ) == 0)
		invalidactions |= MM_FAULT_ACTION_READ;

	if ((range->mmuflags & ARCH_MMU_FLAGS_WRITE) == 0)
		invalidactions |= MM_FAULT_ACTION_WRITE;

	if ((range->mmuflags & ARCH_MMU_FLAGS_NOEXEC))
		invalidactions |= MM_FAULT_ACTION_EXEC;

	if (invalidactions & actions) {
		printf("mm: bad action\n");
		goto cleanup;
	}

	thread_t *thread = current_thread();
	proc_t *proc = thread ? thread->proc : NULL;
	cred_t *cred = proc ? &proc->cred : NULL;

	if (arch_mmu_ispresent(current_mm_context()->pagetable, addr) == false) {
		// page not present in the page tables
		if (range->flags & MM_RANGE_FLAGS_FILE) {
			uintmax_t mapoffset = (uintptr_t)addr - (uintptr_t)range->start;
			if (vfs_iscacheable(range->vnode) == false) {
				// map non cacheable vnodes
				VOP_LOCK(range->vnode);
				__assert(VOP_MMAP(range->vnode, addr, range->offset + mapoffset, mm_mmu_flags_to_vnode_flags(range->mmuflags) | (range->flags & MM_RANGE_FLAGS_SHARED ? V_FFLAGS_SHARED : 0), cred) == 0);
				VOP_UNLOCK(range->vnode);
				status = true;
			} else {
				// cacheable vnode
				page_t *res = NULL;
				int error = mm_cache_get_page(range->vnode, range->offset + mapoffset, &res);

				if (error == ENXIO || error == ENOMEM)  {
					if (error == ENOMEM)
						printf("mm: out of memory to handle getpage (sending SIGBUS)\n");
					// address is past the last page of the file
					signal_signalthread(current_thread(), SIGBUS, true);
					status = true;
				} else if (error) {
					printf("mm: error on mm_cache_get_page(): %d\n", error);
					status = false;
				} else {
					status = arch_mmu_map(current_mm_context()->pagetable, mm_get_page_address(res), addr, range->mmuflags & ~ARCH_MMU_FLAGS_WRITE);
					if (!status) {
						printf("mm: out of memory to map file into address space (sending SIGBUS)\n");
						mm_release_page(mm_get_page_address(res));
						signal_signalthread(current_thread(), SIGBUS, true);
						status = true;
					}
				}
			}
		} else {
			status = true;
			// anonymous memory.
			if (range->mmuflags & ARCH_MMU_FLAGS_WRITE) {
				// writeable range: allocate a new page, zero it and map it
				void *new_page = mm_alloc_page(MEMORY_SECTION_DEFAULT);
				if (new_page == NULL) {
					printf("mm: out of memory to allocate page to satisfy anonymous page-in (sending SIGBUS)\n");
					signal_signalthread(current_thread(), SIGBUS, true);
					goto cleanup;
				}

				memset(MAKE_HHDM(new_page), 0, PAGE_SIZE);

				if (!arch_mmu_map(current_mm_context()->pagetable, new_page, addr, range->mmuflags)) {
					printf("mm: out of memory to map page (sending SIGBUS)\n");
					signal_signalthread(current_thread(), SIGBUS, true);
					mm_release_page(new_page);
				}
			} else {
				// read-only range: map a shared zero page
				if (!arch_mmu_map(current_mm_context()->pagetable, zeropage, addr, range->mmuflags)) {
					printf("mm: out of memory to map zero page into address space (sending SIGBUS)\n");
					signal_signalthread(current_thread(), SIGBUS, true);
				} else {
					mm_hold_page(zeropage);
				}
			}
		}
	} else if (arch_mmu_iswritable(current_mm_context()->pagetable, addr) == false) {
		// page present but not writeable in the page tables
		void *oldphys = arch_mmu_getphysical(current_mm_context()->pagetable, addr);
		page_t *oldpage = mm_get_page(oldphys);

		if (    ((range->flags & MM_RANGE_FLAGS_FILE) && (range->flags & MM_RANGE_FLAGS_SHARED)) ||
			((range->flags & MM_RANGE_FLAGS_FILE) == 0 && oldpage->refcount == 1)) {
			// shared file or anon with refcount == 1, remap it as writable

			arch_mmu_remap(current_mm_context()->pagetable, oldphys, addr, range->mmuflags);
			if ((range->flags & MM_RANGE_FLAGS_FILE) && vfs_iscacheable(range->vnode)) {
				// and if its a cache page, mark it as dirty
				VOP_LOCK(range->vnode);
				mm_cache_make_dirty(mm_get_page(oldphys));
				VOP_UNLOCK(range->vnode);
			}

			status = true;
		} else {
			// do copy on write
			void *newphys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
			if (newphys == NULL) {
				printf("mm: out of memory to do copy on write on address space (sending SIGBUS)\n");
				signal_signalthread(current_thread(), SIGBUS, true);
				status = true;
			} else {
				memcpy(MAKE_HHDM(newphys), MAKE_HHDM(oldphys), PAGE_SIZE);
				arch_mmu_remap(current_mm_context()->pagetable, newphys, addr, range->mmuflags);
				arch_mmu_invalidate_range(addr, PAGE_SIZE);
				if ((range->flags & MM_RANGE_FLAGS_FILE) == 0 || vfs_iscacheable(range->vnode))
					mm_release_page(oldphys);

				status = true;
			}
		}
	} else {
		// another thread already did the work, so just return success
		status = true;
	}

	cleanup:
	MUTEX_RELEASE(&space->lock);
	return status;
}

/*
static int lock_page(mm_space_t *space, void *vaddr) {
	mm_range_t *range = mm_get_range(space, vaddr);
	if (!range)
		return ENOENT;

	void *mapped_address = arch_mmu_getphysical(current_mm_context()->pagetable, vaddr);
	bool present = arch_mmu_ispresent(current_mm_context()->pagetable, vaddr);
	bool writable = arch_mmu_iswritable(current_mm_context()->pagetable, vaddr);
	bool file_mapping = range->flags & MM_RANGE_FLAGS_FILE;
	bool private = (range->flags & MM_RANGE_FLAGS_SHARED) == 0 || file_mapping == false;
	void *real_phys;

	// TODO anon with refcount == 1 can go into another cas
	// TODO read-only permission checking for DMA
	// TODO shared anonymous mappings are not handled properly by the kernel
	if (present && writable == false && private)) {
		// CoW cases (private mapping)
		real_phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
		if (real_phys == NULL)
			return ENOMEM;

		memcpy(MAKE_HHDM(real_phys), MAKE_HHDM(mapped_address), PAGE_SIZE);

		arch_mmu_remap(current_mm_context()->pagetable, real_phys, vaddr, range->mmuflags);
		arch_mmu_invalidate_range(vaddr, PAGE_SIZE);
		if (file_mapping == false || vfs_iscacheable(range->vnode))
			mm_release_page(mapped_address);
	} else if (present == false && private) {
		// not present private mapping case
		real_phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
		if (real_phys == NULL)
	}
}
*/

void *mm_get_physical_address(void *addr, int flags) {
	void *aligned_addr = (void *)ROUND_DOWN((uintptr_t)addr, PAGE_SIZE);

	mm_space_t *space = getspace(aligned_addr);
	if (space == NULL)
		return NULL;

	MUTEX_ACQUIRE(&space->lock);

	void *physical;
/*
	if ((flags & MM_GET_PHYSICAL_ADDRESS_FLAGS_LOCK) && lock_page(space, aligned_addrr)) {
		physical = NULL;
		goto leave;
	}*/

	physical = arch_mmu_getphysical(current_mm_context()->pagetable, aligned_addr);

	if (flags & MM_GET_PHYSICAL_ADDRESS_FLAGS_HOLD)
		mm_hold_page(physical);

	MUTEX_RELEASE(&space->lock);
	return physical + ((uintptr_t)addr - (uintptr_t)aligned_addr);
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

	mm_space_t *space = getspace(addr);
	if (space == NULL)
		return NULL;

	if (space == &kernelspace)
		mmuflags |= ARCH_MMU_FLAGS_GLOBAL;

	MUTEX_ACQUIRE(&space->lock);
	mm_range_t *range = NULL;

	void *start = mm_get_free_range(space, addr, size);
	void *retaddr = NULL;
	if (((flags & MM_RANGE_FLAGS_EXACT) && start != addr) || start == NULL)
		goto cleanup;

	range = mm_alloc_range();
	if (range == NULL)
		goto cleanup;

	if (flags & MM_RANGE_FLAGS_REPLACE) {
		__assert(addr);
		retaddr = addr;
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
		retaddr = start;
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
				retaddr = NULL;
				goto cleanup;
			}
		}
	} else if (flags & MM_RANGE_FLAGS_ALLOCATE) {
		// allocate to virtual memory
		for (uintmax_t i = 0; i < size; i += PAGE_SIZE) {
			void *allocated = mm_alloc_page(MEMORY_SECTION_DEFAULT);
			if (allocated == NULL) {
				retaddr = NULL;
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
				retaddr = NULL;
				goto cleanup;
			}
			memset(MAKE_HHDM(allocated), 0, PAGE_SIZE);
		}
	}

	mm_insert_range(space, range);
	cleanup:
	if (retaddr == NULL && range)
		mm_free_range(range);

	MUTEX_RELEASE(&space->lock);
	return retaddr;
}

void mm_unmap(void *addr, size_t size, int flags) {
	addr = (void *)ROUND_DOWN((uintptr_t)addr, PAGE_SIZE);

	if (flags & MM_RANGE_FLAGS_PAGESIZE)
		size *= PAGE_SIZE;
	else
		size = ROUND_UP(size, PAGE_SIZE);

	if (size == 0)
		return;

	mm_space_t *space = getspace(addr);
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

static scache_t *ctxcache;

static bool ctxctor(scache_t *cache, void *obj) {
	mm_context_t *ctx = obj;
	ctx->space.start = USERSPACE_START;
	ctx->space.end = USERSPACE_END;
	MUTEX_INIT(&ctx->space.lock);
	ctx->space.ranges = NULL;
	return true;
}

mm_context_t *mm_create_context() {
	if (ctxcache == NULL) {
		ctxcache = slab_newcache(sizeof(mm_context_t), 0, ctxctor, NULL);
		__assert(ctxcache);
	}

	mm_context_t *ctx = slab_allocate(ctxcache);
	if (ctx == NULL)
		return NULL;

	ctx->pagetable = arch_mmu_newtable();
	if (ctx->pagetable == NULL) {
		slab_free(ctxcache, ctx);
		return NULL;
	}

	return ctx;
}

void mm_destroy_context(mm_context_t *context) {
	mm_context_t *oldctx = current_thread()->mmctx;
	mm_switch_context(context);
	mm_unmap(context->space.start, context->space.end - context->space.start, 0);
	mm_switch_context(oldctx);
	arch_mmu_destroytable(context->pagetable);

	context->space.ranges = NULL;
	slab_free(ctxcache, context);
}

mm_context_t *mm_fork_context(mm_context_t *oldcontext) {
	mm_context_t *newcontext = mm_create_context();
	if (newcontext == NULL)
		return NULL;

	MUTEX_ACQUIRE(&oldcontext->space.lock);

	rbtree_t *rbtree = oldcontext->space.ranges ? rbtree_first(oldcontext->space.ranges) : NULL;
	while (rbtree) {
		mm_range_t *range = container_of(rbtree, mm_range_t, rbtree_node);
		mm_range_t *newrange = mm_alloc_range();
		if (newrange == NULL)
			goto error;

		memcpy(newrange, range, sizeof(mm_range_t));

		rbtree_insert(&newcontext->space.ranges, &newrange->rbtree_node, mm_range_compare);
		if (range->flags & MM_RANGE_FLAGS_FILE)
			VOP_HOLD(range->vnode);

		// copy any pages that are mapped

		for (uintptr_t offset = 0; offset < newrange->size; offset += PAGE_SIZE) {
			// XXX some types of mappings, like framebuffer shared mappings, will break if done this way
			void *vaddr = (void *)((uintptr_t)newrange->start + offset);
			void *phys = arch_mmu_getphysical(oldcontext->pagetable, vaddr);
			if (phys == NULL)
				continue;

			if (arch_mmu_map(newcontext->pagetable, phys, vaddr, newrange->mmuflags & ~ARCH_MMU_FLAGS_WRITE) == false)
				goto error;

			mm_hold_page(phys);

			arch_mmu_remap(oldcontext->pagetable, phys, vaddr, newrange->mmuflags & ~ARCH_MMU_FLAGS_WRITE);
		}

		rbtree = rbtree_successor(rbtree);
	}

	// TODO do only userspace invalidation as to not send ipi to all cores
	arch_mmu_invalidate_range(NULL, 0);

	MUTEX_RELEASE(&oldcontext->space.lock);
	return newcontext;
	error:
	MUTEX_RELEASE(&oldcontext->space.lock);
	mm_destroy_context(newcontext);
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
	MUTEX_INIT(&kernelspace.lock);

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
	zeropage = mm_alloc_page(MEMORY_SECTION_DEFAULT);
	memset(MAKE_HHDM(zeropage), 0, PAGE_SIZE);

	printspace(&kernelspace);
}

INIT_ROUTINE_DEFINE(mm, INIT_ROUTINE_FLAGS_NONE, mm_init, mmu, slab_early);

void mm_apinit() {
	mm_switch_context(&mm_kernel_ctx);
}
