#ifndef _MMU_H_INCLUDE
#define _MMU_H_INCLUDE

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define ARCH_MMU_FLAG_READ (uint64_t)1
#define ARCH_MMU_FLAG_WRITE (uint64_t)2
#define ARCH_MMU_FLAG_USER (uint64_t)4
#define ARCH_MMU_FLAG_NOEXEC ((uint64_t)1 << 63)

typedef uint64_t mmuflags_t;
typedef uint64_t * pagetableptr_t; // physical address

bool arch_mmu_map(pagetableptr_t table, void *paddr, void *vaddr, mmuflags_t flags);
void arch_mmu_invalidate(void *vaddr);
void arch_mmu_unmap(pagetableptr_t table, void *vaddr);
void arch_mmu_remap(pagetableptr_t table, void *paddr, void *vaddr, mmuflags_t flags);
void arch_mmu_switch(pagetableptr_t table);
void *arch_mmu_getphysical(pagetableptr_t table, void *vaddr);
void arch_mmu_init();

#endif
