#ifndef _MMU_H_INCLUDE
#define _MMU_H_INCLUDE

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define KERNELSPACE_START (void *)0xffff800000000000
#define KERNELSPACE_END   (void *)0xffffffffffffffff
#define USERSPACE_START   (void *)0x0000000000001000
#define USERSPACE_END     (void *)0x0000800000000000

#define PAGE_SIZE 4096
#define ARCH_MMU_FLAGS_READ (uint64_t)1
#define ARCH_MMU_FLAGS_WRITE (uint64_t)2
#define ARCH_MMU_FLAGS_USER (uint64_t)4
#define ARCH_MMU_FLAGS_NOEXEC ((uint64_t)1 << 63)

#define ARCH_MMU_FLAGS_WB 0
#define ARCH_MMU_FLAGS_WT (1 << 3)
#define ARCH_MMU_FLAGS_UC (1 << 4)

typedef uint64_t mmuflags_t;
typedef uint64_t * pagetableptr_t; // physical address

void arch_mmu_destroytable(pagetableptr_t table);
bool arch_mmu_map(pagetableptr_t table, void *paddr, void *vaddr, mmuflags_t flags);
void arch_mmu_invalidate(void *vaddr);
void arch_mmu_unmap(pagetableptr_t table, void *vaddr);
void arch_mmu_remap(pagetableptr_t table, void *paddr, void *vaddr, mmuflags_t flags);
void arch_mmu_switch(pagetableptr_t table);
void *arch_mmu_getphysical(pagetableptr_t table, void *vaddr);
bool arch_mmu_ispresent(pagetableptr_t table, void *vaddr);
bool arch_mmu_iswritable(pagetableptr_t table, void *vaddr);
bool arch_mmu_isdirty(pagetableptr_t table, void *vaddr);
pagetableptr_t arch_mmu_newtable();
void arch_mmu_init();
void arch_mmu_apswitch();
void arch_mmu_tlbshootdown(void *page);

#endif
