#ifndef _PAGE_H
#define _PAGE_H

#include <stddef.h>
#include <stdint.h>

#define MEMORY_SECTION_COUNT 3
#define MEMORY_SECTION_1MB 0
#define MEMORY_SECTION_4GB 1
#define MEMORY_SECTION_DEFAULT 2

#define PAGE_FLAGS_FREE 1
#define PAGE_FLAGS_TRUNCATED 2
#define PAGE_FLAGS_PINNED 4
#define PAGE_FLAGS_DIRTY 8
#define PAGE_FLAGS_READY 16
#define PAGE_FLAGS_ERROR 32
#define PAGE_FLAGS_VNODE_SYNCING 64

typedef struct page_t {
	struct vnode_t *backing;
	uintmax_t offset;
	struct page_t *hash_next;
	struct page_t *hash_prev;
	struct page_t *vnode_next;
	struct page_t *vnode_prev;
	union {
		struct {
			struct page_t *free_next;
			struct page_t *free_prev;
		};
		struct {
			struct page_t *write_next;
			struct page_t *write_prev;
		};
	};
	uintmax_t refcount;
	int flags;
} page_t;

void *mm_alloc_page(int section);
page_t *mm_get_page(void *addr);
void *mm_get_page_address(page_t *);
void mm_hold_page(void *addr);
void mm_release_page(void *addr);
void mm_force_free_page(void *address, size_t count);
void *mm_alloc_pages(size_t size, int section);
void mm_release_range(void *addr, size_t size);
void mm_page_init();
void mm_get_page_statistics(size_t *total_pages, size_t *free_pages);

extern uintptr_t hhdm_base;

#define MAKE_HHDM(x) (void *)((uintptr_t)x + hhdm_base)
#define FROM_HHDM(x) (void *)((uintptr_t)x - hhdm_base)

#endif
