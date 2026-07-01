#ifndef _MM_H
#define _MM_H

#include <kernel/page.h>
#include <kernel/vfs.h>

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

#endif
