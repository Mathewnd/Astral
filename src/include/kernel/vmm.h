#ifndef _VMM_H_INCLUDE
#define _VMM_H_INCLUDE

#include <kernel/vfs.h>
#include <arch/mmu.h>
#include <stdbool.h>
#include <stddef.h>

#define KERNEL_SPACE_START 0xFFFF800000000000
#define KERNEL_SPACE_END   0xFFFFFFFFFFFFFFFF
#define USER_SPACE_START   0x0
#define USER_SPACE_END     (void*)0x00007FFFFFFFFFFF

#define USER_ALLOC_START   (void*)0x1000

#define VMM_TYPE_FREE 0
#define VMM_TYPE_ANON 1
#define VMM_TYPE_FILE 2

struct vmm_cacheheader;

typedef struct{
	int lock;
	size_t firstfree;
	struct vmm_cacheheader* next;
	size_t freecount;
} vmm_cacheheader;

struct vmm_mapping;

typedef struct _vmm_mapping {
	struct _vmm_mapping *next;
	struct _vmm_mapping *prev;
	vmm_cacheheader* cache;
	void* start;
	void* end;
	size_t mmuflags;
	size_t type;
	void*  data;
	size_t offset;
} vmm_mapping;


#define VMM_CACHE_ENTRY_COUNT ((PAGE_SIZE - sizeof(vmm_cacheheader)) / sizeof(vmm_mapping))

typedef struct{
	vmm_cacheheader header;
	vmm_mapping mappings[VMM_CACHE_ENTRY_COUNT];
} vmm_cache;

typedef struct {
	vmm_mapping* userstart;
	arch_mmu_tableptr context;
	int lock;
} vmm_context;

void vmm_init();

void vmm_destroy(vmm_context* ctx);
bool 		vmm_dealwithrequest(void* addr, long error, bool user);
bool 		vmm_setused(void* addr, size_t pagec, size_t mmuflags);
bool		vmm_unmap(void* addr, size_t pagec);
bool		vmm_map(void* paddr, void* vaddr, size_t pagec, size_t mmuflags);
void*		vmm_alloc(size_t pagec, size_t mmuflags);
bool		vmm_setfree(void* addr, size_t pagec);
bool		vmm_allocnowat(void* addr, size_t mmuflags, size_t size);
void*		vmm_allocfrom(void* addr, size_t mmuflags, size_t size);
int		vmm_mapfile(vnode_t* node, void* addr, size_t len, size_t offset, size_t mmuflags);
vmm_context*	vmm_newcontext();
void		vmm_switchcontext(vmm_context*);
int		vmm_fork(vmm_context* oldctx, vmm_context* newctx);

#endif
