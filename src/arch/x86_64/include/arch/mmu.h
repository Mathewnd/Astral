#ifndef _MMU_H_INCLUDE
#define _MMU_H_INCLUDE

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096

#define ARCH_MMU_MAP_READ         (1 << 0)
#define ARCH_MMU_MAP_WRITE        (1 << 1)
#define ARCH_MMU_MAP_USER         (1 << 2)
#define ARCH_MMU_MAP_NOEXEC       (1 << 63)
#define ARCH_MMU_MAP_PAGESIZE     (1 << 7)

typedef uint64_t* arch_mmu_tableptr;

int arch_mmu_map(arch_mmu_tableptr, void*, void*, size_t);
void arch_mmu_unmap(arch_mmu_tableptr, void*);
void arch_mmu_init();


#endif
