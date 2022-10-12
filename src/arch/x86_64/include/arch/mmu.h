#ifndef _MMU_H_INCLUDE
#define _MMU_H_INCLUDE

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PAGE_SIZE 4096

#define ARCH_MMU_MAP_READ         (uint64_t)(1 << 0)
#define ARCH_MMU_MAP_WRITE        (uint64_t)(1 << 1)
#define ARCH_MMU_MAP_USER         (uint64_t)(1 << 2)
#define ARCH_MMU_MAP_NOEXEC       (uint64_t)(1 << 63)
#define ARCH_MMU_MAP_PAGESIZE     (uint64_t)(1 << 7)
#define ARCH_MMU_MAP_ACCESSED	  (uint64_t)(1 << 5)

#define ARCH_MMU_ERROR_WRITE 2
#define ARCH_MMU_ERROR_PRESENT 1
#define ARCH_MMU_ERROR_USER 4
#define ARCH_MMU_ERROR_INSTFETCH 16

typedef uint64_t* arch_mmu_tableptr;

void arch_mmu_destroy(arch_mmu_tableptr context);
int arch_mmu_map(arch_mmu_tableptr, void*, void*, size_t);
bool arch_mmu_isaccessed(arch_mmu_tableptr, void*);
void* arch_mmu_getphysicaladdr(arch_mmu_tableptr, void*);
bool arch_mmu_ismapped(arch_mmu_tableptr, void*);
void arch_mmu_unmap(arch_mmu_tableptr, void*);
void arch_mmu_init();
void arch_mmu_apinit();
arch_mmu_tableptr arch_mmu_newcontext();
void arch_mmu_switchcontext(arch_mmu_tableptr);
void arch_mmu_changeflags(arch_mmu_tableptr context, void* addr, size_t flags, size_t count);

#endif
