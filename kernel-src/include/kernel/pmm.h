#ifndef _PMM_H
#define _PMM_H

#include <stddef.h>
#include <stdint.h>

#define PMM_SECTION_COUNT 3
#define PMM_SECTION_1MB 0
#define PMM_SECTION_4GB 1
#define PMM_SECTION_DEFAULT 2

void *pmm_alloc(size_t count, int flags);
void  pmm_free(void *addr, size_t count);
void  pmm_init();

extern uintptr_t hhdmbase;

#define MAKE_HHDM(x) (void *)((uintptr_t)x + hhdmbase)
#define FROM_HHDM(x) (void *)((uintptr_t)x - hhdmbase)

#endif
