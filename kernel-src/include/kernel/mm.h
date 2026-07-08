#ifndef _MM_H
#define _MM_H

#include <arch/mmu.h>
#include <mutex.h>
#include <kernel/page.h>
#include <kernel/vfs.h>
#include <rbtree.h>

#define MM_RANGE_FLAGS_PAGESIZE 1
#define MM_RANGE_FLAGS_ALLOCATE 2
#define MM_RANGE_FLAGS_PHYSICAL 4
#define MM_RANGE_FLAGS_FILE     8
#define MM_RANGE_FLAGS_EXACT   16
#define MM_RANGE_FLAGS_SHARED  32
#define MM_RANGE_FLAGS_REPLACE 64
#define MM_RANGE_FLAGS_CREDCHECK 128

#define MM_PERMANENT_RANGE_FLAGS_MASK (MM_RANGE_FLAGS_FILE | MM_RANGE_FLAGS_SHARED | MM_RANGE_FLAGS_PHYSICAL)

#define MM_FAULT_ACTION_READ 1
#define MM_FAULT_ACTION_WRITE 2
#define MM_FAULT_ACTION_EXEC 4

typedef struct {
	vnode_t *node;
	uintmax_t offset;
} mm_map_file_desc_t;

typedef struct mm_range_t{
	rbtree_t rbtree_node;
	void *start;
	size_t size;
	int flags;
	mmuflags_t mmuflags;
	union {
		struct {
			vnode_t *vnode;
			size_t offset;
		};
	};
} mm_range_t;

typedef struct {
	mutex_t lock;
	rbtree_t *ranges;
	void *start;
	void *end;

	mm_range_t *last_pagefault;
} mm_space_t;

typedef struct {
	mm_space_t space;
	pagetableptr_t pagetable;
} mm_context_t;

extern mm_context_t mm_kernel_ctx;

static inline mmuflags_t mm_vnode_flags_to_mmu_flags(int flags) {
	mmuflags_t mmuflags = ARCH_MMU_FLAGS_USER;
	if (flags & V_FFLAGS_READ)
		mmuflags |= ARCH_MMU_FLAGS_READ;
	if (flags & V_FFLAGS_WRITE)
		mmuflags |= ARCH_MMU_FLAGS_WRITE;
	if ((flags & V_FFLAGS_EXEC) == 0)
		mmuflags |= ARCH_MMU_FLAGS_NOEXEC;

	return mmuflags;
}

static inline int mm_mmu_flags_to_vnode_flags(mmuflags_t mmuflags) {
	int flags = 0;
	if (mmuflags & ARCH_MMU_FLAGS_READ)
		flags |= V_FFLAGS_READ;
	if (mmuflags & ARCH_MMU_FLAGS_WRITE)
		flags |= V_FFLAGS_WRITE;
	if ((mmuflags & ARCH_MMU_FLAGS_NOEXEC) == 0)
		flags |= V_FFLAGS_EXEC;

	return flags;
}

int mm_change_mmu_flags(void *base, size_t size, mmuflags_t mmuflags, int flags);
void mm_destroy_context(mm_context_t *context);
mm_context_t *mm_fork_context(mm_context_t *old_context);
void *mm_map(void *addr, size_t size, int flags, mmuflags_t mmuflags, void *private);
void mm_unmap(void *addr, size_t size, int flags);
bool mm_handle_page_fault(void *addr, bool user, int actions);
mm_context_t *mm_create_context();
void mm_switch_context(mm_context_t *ctx);

#define MM_GET_PHYSICAL_ADDRESS_FLAGS_HOLD 1
#define MM_GET_PHYSICAL_ADDRESS_FLAGS_LOCK 2
#define MM_GET_PHYSICAL_ADDRESS_FLAGS_LOCK_HINT_READ 4
#define MM_GET_PHYSICAL_ADDRESS_FLAGS_LOCK_HINT_WRITE 8
void *mm_get_physical_address(void *addr, int flags);
void mm_apinit();
void mm_init();

extern size_t mm_cache_cached_pages;

void mm_cache_init(void);
int mm_cache_get_page(vnode_t *vnode, uintmax_t offset, page_t **res);
int mm_cache_take_page(page_t *page);
int mm_cache_make_dirty(page_t *page);
int mm_cache_truncate(vnode_t *vnode, uintmax_t offset);
int mm_cache_sync_vnode(vnode_t *vnode, uintmax_t startoffset, size_t size);
int mm_cache_push_page(vnode_t *vnode, uintmax_t offset, page_t *page);
int mm_cache_sync(void);
int mm_cache_evict(page_t *page);

void mm_range_init(void);
mm_range_t *mm_alloc_range(void);
void mm_free_range(mm_range_t *range);
mm_range_t *mm_get_range(mm_space_t *space, void *addr);
void *mm_get_free_range(mm_space_t *space, void *addr, size_t size);
void mm_insert_range(mm_space_t *space, mm_range_t *new_range);
void mm_destroy_range(mm_range_t *range, uintmax_t offset, size_t size, int flags);
int mm_change_range(mm_space_t *space, void *address, size_t size, bool free, int flags, mmuflags_t new_mmuflags);

#endif
