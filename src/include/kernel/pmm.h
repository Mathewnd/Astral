#ifndef _PMM_H_INCLUDE
#define _PMM_H_INCLUDE

#include <stddef.h>

void pmm_init();
void* pmm_alloc(size_t);
void* pmm_hhdmalloc(size_t);
void pmm_hhdmfree(void*, size_t);
void pmm_free(void*, size_t);
void pmm_setused(void*, size_t);

extern void* limine_hhdm_offset;
extern void* pmm_usabletop;

#endif
