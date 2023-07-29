#ifndef _PMM_H
#define _PMM_H

#include <stddef.h>
#include <stdint.h>

#define PMM_SECTION_COUNT 3
#define PMM_SECTION_1MB 0
#define PMM_SECTION_4GB 1
#define PMM_SECTION_DEFAULT 2

#define PAGE_FLAGS_FREE 1

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

void *pmm_allocpage(int section);
page_t *pmm_getpage(void *addr);
void pmm_hold(void *addr);
void pmm_release(void *addr);
void pmm_makefree(void *address, size_t count);
void *pmm_alloc(size_t size, int section);
void pmm_free(void *addr, size_t size);
void pmm_init();

extern uintptr_t hhdmbase;

#define MAKE_HHDM(x) (void *)((uintptr_t)x + hhdmbase)
#define FROM_HHDM(x) (void *)((uintptr_t)x - hhdmbase)

#endif
