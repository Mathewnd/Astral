#include <kernel/pagealloc.h>
#include <kernel/slab.h>
#include <arch/mmu.h>
#include <arch/spinlock.h>
#include <kernel/pmm.h>
#include <string.h>

typedef struct _page_allocation{
        struct _page_allocation* next;
        void* start;
        size_t size;
} page_allocation;

static int lock;
static page_allocation* list;

void* pageallocator_alloc(size_t size){
	
	page_allocation* alloc = slab_alloc(sizeof(page_allocation));
	
	if(!alloc)
		return NULL;

	size_t pagec = size / PAGE_SIZE + (size % PAGE_SIZE ? 1 : 0);

	// use vmm?
	
	alloc->start = pmm_hhdmalloc(pagec);

	if(!alloc->start){
		slab_free(alloc);
		return NULL;
	}

	alloc->size  = size;
	
	spinlock_acquire(&lock);

	if(list)
		alloc->next = list;
	
	list = alloc;
	
	spinlock_release(&lock);
	
	memset(alloc->start, 0, pagec*PAGE_SIZE);

	return alloc->start;
	
}

bool pageallocator_free(void* addr){
	spinlock_acquire(&lock);
	
	page_allocation* it = list;
	page_allocation* prev = NULL;

	while(it){
		void* end = it->start + it->size;
		if(it->start <= addr && end > addr)
			break;
		prev = it;
		it = it->next;
	}
	
	if(!it){
		spinlock_release(&lock);
		return false;
	}

	if(!prev)
		list = it->next;
	else
		prev->next = it->next;

	spinlock_release(&lock);


	size_t pagec = it->size / PAGE_SIZE + (it->size % PAGE_SIZE ? 1 : 0);
		
	// use vmm?
	pmm_hhdmfree(it->start, pagec);
	slab_free(it);

	return true;
}

void* pageallocator_realloc(void* addr, size_t size, bool* nonexistant){
	
	spinlock_acquire(&lock);
	
	page_allocation* it = list;
	page_allocation* prev = NULL;

	while(it){
		void* end = it->start + it->size;
		if(it->start <= addr && end > addr)
			break;
		prev = it;
		it = it->next;
	}

	if(!it){
		*nonexistant = 1;
		spinlock_release(&lock);
		return NULL;
	}
	size_t oldpagec = it->size / PAGE_SIZE + (it->size % PAGE_SIZE ? 1 : 0);
	size_t newpagec = size / PAGE_SIZE + (size % PAGE_SIZE ? 1 : 0);
	
	if(oldpagec == newpagec){
		spinlock_release(&lock);
		return it->start;
	}

	if(!prev)
		list = it->next;
	else
		prev->next = it->next;

	spinlock_release(&lock);
	
	void* new = pageallocator_alloc(size);

	if(!new){
		*nonexistant = 0;
		return NULL;
	}
	
	memcpy(new, it->start, it->size);
	
	// use vmm?
	pmm_hhdmfree(it->start, oldpagec);
	slab_free(it);

	return new;

}
