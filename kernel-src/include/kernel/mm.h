#ifndef _MM_H
#define _MM_H

#include <kernel/page.h>
#include <kernel/vfs.h>
#include <kernel/vmm.h>

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
vmmrange_t *mm_alloc_range(void);
void mm_free_range(vmmrange_t *range);
vmmrange_t *mm_get_range(vmmspace_t *space, void *addr);
void *mm_get_free_range(vmmspace_t *space, void *addr, size_t size);
void mm_insert_range(vmmspace_t *space, vmmrange_t *new_range);
void mm_destroy_range(vmmrange_t *range, uintmax_t offset, size_t size, int flags);
int mm_change_range(vmmspace_t *space, void *address, size_t size, bool free, int flags, mmuflags_t new_mmuflags);

#endif
