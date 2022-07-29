#include <kernel/slab.h>
#include <arch/spinlock.h>
#include <arch/mmu.h>
#include <kernel/pmm.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SLAB_SIZE PAGE_SIZE

// TODO have the slabs be in virtual memory rather than in the hhdm
<<<<<<< HEAD
=======
// this isn't great but enough for an initial implementation
// redo this later
>>>>>>> 3e78844 (Initial slab implementation)

// the layout of a slab in this implementation:
// header (slab_t)
// body is made of group several groups
// a group is:
// bitmap for the next 8 entries
// the 8 entries


typedef struct _slab {
	int lock;
	uint16_t freecount;
	uint16_t firstfree;
	uint16_t entrysize;
	struct _slab* next;
} __attribute__((packed))slab_t;

typedef struct {
	size_t entrysize;
	size_t groupsize;
	size_t groupcount;
	slab_t* first;
} slabdesc;

static slabdesc slabs[7];

static inline slabdesc* getslabdesc(size_t size){

	slabdesc *desc = NULL;

	for(size_t i = 0; i < 7 && desc == NULL; ++i){
		if(slabs[i].entrysize >= size)
			desc = &slabs[i];
	}

	return desc;

}

static void free(void* addr){
	
	// align down to page size
	
	slab_t *slab = (size_t)addr & ~(0xFFF);
	spinlock_acquire(&slab->lock);
	
	// find group that contains the address

	uint8_t* group = (void*)slab + sizeof(slab_t);

	size_t groupsize = slab->entrysize*8 + 1;

	for(;group < slab + SLAB_SIZE; group += groupsize){
		if(group + groupsize <= addr)
			continue;

		size_t first = group+1;
		
		size_t bit = ((size_t)addr - first) / slab->entrysize;
		
		*group &= ~(1 << bit);

		break;

	}
	
	++slab->freecount;

	spinlock_release(&slab->lock);
}

static void* allocin(slabdesc* desc, slab_t* slab){
	
	uint8_t* groups = (void*)slab + sizeof(slab_t);
	void* allocval = NULL;

	for(size_t i = 0; i < desc->groupcount; ++i, groups += desc->groupsize){
		if(*groups == 0xFF) continue;


		// group has a free member
		
		size_t bit = 0;

		for(; bit < 8; ++bit){
			if((*groups >> bit) & 1)
				continue;
			break;
		}
		
		*groups |= (1 << bit);

		allocval = groups + 1 + desc->entrysize * bit;

		break;
		
	}

	if(allocval){
		--slab->freecount;
		memset(allocval, 0, desc->entrysize);
	}

	
	return allocval;
	
}

slab_t* newslab(slabdesc *desc){
	slab_t* slab = pmm_hhdmalloc(SLAB_SIZE / PAGE_SIZE);
	if(!slab) return NULL;
	memset(slab, 0, SLAB_SIZE);
	slab->entrysize = desc->entrysize;
	slab->freecount = desc->groupcount * 8;
	return slab;
}

void* slab_alloc(size_t size){
	slabdesc* desc = getslabdesc(size);
	if(!desc) return NULL;
	
	slab_t* slab = desc->first;
	slab_t* last;
	
	while(slab){
		spinlock_acquire(&slab->lock);
		if(slab->freecount == 0){
			spinlock_release(&slab->lock);
			last = slab;
			slab = slab->next;
			continue;
		}
		
		break;

	}
	
	if(!slab){
		slab = newslab(desc);
		if(!slab){
			spinlock_release(&last->lock);
			return NULL;
		}
		spinlock_acquire(&slab->lock);
		last->next = slab;
		spinlock_release(&last->lock);
	}

	void* alloc = allocin(desc, slab);

	spinlock_release(&slab->lock);

	return alloc;

}

void slab_free(void* addr){
	
	free(addr);

}

void slab_init(){
	for(size_t i = 0; i < 7; ++i){
		slabs[i].entrysize = 1 << (i + 3);
		slabs[i].groupsize = slabs[i].entrysize * 8 + 1;
		slabs[i].groupcount = (SLAB_SIZE - sizeof(slab_t)) / slabs[i].groupsize;
		slab_t* slab = newslab(&slabs[i]);
		if(!slab) _panic("Out of memory!", 0);
		slabs[i].first = slab;
		
	}
}
