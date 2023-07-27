#ifndef _VMM_H
#define _VMM_H

#include <arch/mmu.h>
#include <mutex.h>
#include <kernel/vfs.h>

#define VMM_FLAGS_PAGESIZE 1
#define VMM_FLAGS_ALLOCATE 2
#define VMM_FLAGS_PHYSICAL 4
#define VMM_FLAGS_FILE     8
#define VMM_FLAGS_EXACT   16
#define VMM_FLAGS_SHARED  32
#define VMM_FLAGS_COPY    64

#define VMM_PERMANENT_FLAGS_MASK (VMM_FLAGS_FILE | VMM_FLAGS_SHARED | VMM_FLAGS_COPY)

#define VMM_ACTION_READ 1
#define VMM_ACTION_WRITE 2
#define VMM_ACTION_EXEC 4

#define VMM_DESTROY_FLAGS_NOSYNC 1

typedef struct {
	vnode_t *node;
	uintmax_t offset;
} vmmfiledesc_t;

struct vmmcache_t;
typedef struct vmmrange_t{
	struct vmmrange_t *next;
	struct vmmrange_t *prev;
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
} vmmrange_t;

typedef struct {
	mutex_t lock;
	struct vmmcache_t *next;
	size_t freecount;
	uintmax_t firstfree;
} vmmcacheheader_t;

#define VMM_RANGES_PER_CACHE (PAGE_SIZE - sizeof(vmmcacheheader_t)) / sizeof(vmmrange_t)

typedef struct vmmcache_t {
	vmmcacheheader_t header;
	vmmrange_t ranges[VMM_RANGES_PER_CACHE];
} vmmcache_t;

typedef struct {
	mutex_t lock;
	vmmrange_t *ranges;
	void *start;
	void *end;
} vmmspace_t;

typedef struct {
	vmmspace_t space;
	pagetableptr_t pagetable;
} vmmcontext_t;

extern vmmcontext_t vmm_kernelctx;

static inline mmuflags_t vnodeflagstommuflags(int flags) {
	mmuflags_t mmuflags = ARCH_MMU_FLAGS_USER;
	if (flags & V_FFLAGS_READ)
		mmuflags |= ARCH_MMU_FLAGS_READ;
	if (flags & V_FFLAGS_WRITE)
		mmuflags |= ARCH_MMU_FLAGS_WRITE;
	if ((flags & V_FFLAGS_EXEC) == 0)
		mmuflags |= ARCH_MMU_FLAGS_NOEXEC;

	return mmuflags;
}

static inline int mmuflagstovnodeflags(mmuflags_t mmuflags) {
	int flags = 0;
	if (mmuflags & ARCH_MMU_FLAGS_READ)
		flags |= V_FFLAGS_READ;
	if (mmuflags & ARCH_MMU_FLAGS_WRITE)
		flags |= V_FFLAGS_WRITE;
	if ((mmuflags & ARCH_MMU_FLAGS_NOEXEC) == 0)
		flags |= V_FFLAGS_EXEC;

	return flags;
}

void vmm_destroycontext(vmmcontext_t *context);
vmmcontext_t *vmm_fork(vmmcontext_t *oldcontext);
void *vmm_map(void *addr, size_t size, int flags, mmuflags_t mmuflags, void *private);
void vmm_unmap(void *addr, size_t size, int flags);
bool vmm_pagefault(void *addr, bool user, int actions);
vmmcontext_t *vmm_newcontext();
void vmm_switchcontext(vmmcontext_t *ctx);
void vmm_init();

#endif
