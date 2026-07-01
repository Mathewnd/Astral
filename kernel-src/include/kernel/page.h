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
	struct page_t *hashnext;
	struct page_t *hashprev;
	struct page_t *vnodenext;
	struct page_t *vnodeprev;
	union {
		struct {
			struct page_t *freenext;
			struct page_t *freeprev;
		};
		struct {
			struct page_t *writenext;
			struct page_t *writeprev;
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

extern uintptr_t hhdmbase;

#define MAKE_HHDM(x) (void *)((uintptr_t)x + hhdmbase)
#define FROM_HHDM(x) (void *)((uintptr_t)x - hhdmbase)

#endif
