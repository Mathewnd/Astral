#include <kernel/pmm.h>
#include <arch/panic.h>
#include <limine.h>
#include <stdio.h>
#include <kernel/spinlock.h>

#define PAGE_SIZE 4096

#define STATE_USED 1
#define STATE_FREE 0

void* limine_hddm_offset;

volatile struct limine_hhdm_request hhdm_req = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

volatile struct limine_memmap_request memmap_req = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

void* hhdmoffset;
size_t bitmappages;
size_t* bitmap = 0;
size_t usablememsize = 0;
size_t totalmemsize  = 0;
void* lastfree = 0;
int lock = 0;

static void setstate(void* addr, size_t state){
	if(addr > (void*)totalmemsize) return;
	if(addr >= hhdmoffset) addr -= hhdmoffset;
	
	size_t page = (size_t)addr / PAGE_SIZE;

	// find the exact size_t sized chunk that the entry resides in	
	size_t chunk = page / 8 / sizeof(size_t);
	
	// where inside the size_t is it

	size_t offset = page % (sizeof(size_t)*8);

	size_t entry = bitmap[chunk];
	entry &= ~(1 << offset);
	entry |= (state & 1) << offset;
	bitmap[chunk] = entry;
	
}

void pmm_setused(void* addr, size_t count){
	if(count == 0) return;
	spinlock_acquire(&lock);
	
	for(size_t i = 0; i < count; ++i)	
		setstate(addr + PAGE_SIZE*i, STATE_USED);

	spinlock_release(&lock);
}

void pmm_setfree(void* addr, size_t count){
	if(count == 0) return;
	spinlock_acquire(&lock);
	
	for(size_t i = 0; i < count; ++i)	
		setstate(addr + PAGE_SIZE*i, STATE_FREE);
	
	lastfree = addr < lastfree ? addr : lastfree;

	spinlock_release(&lock);
}

void pmm_init(){
	
	if(!hhdm_req.response)
		_panic("No hhdm request response", 0);
	if(!memmap_req.response)
		_panic("No memmap request response", 0);
	
	hhdmoffset = hhdm_req.response->offset;
	
	printf("Limine hhdm at %p\n", hhdmoffset);

	// find usable memory size
	
	struct limine_memmap_entry *lastusable;

	for(size_t i = 0; i < memmap_req.response->entry_count; ++i){
		struct limine_memmap_entry *current = memmap_req.response->entries[i];
		
		totalmemsize += current->length;

		if(!current->type == LIMINE_MEMMAP_USABLE)
			continue;

		usablememsize += current->length;

	}

	
	printf("Memory Size: %lu pages (%lu MB)\n", usablememsize / PAGE_SIZE, usablememsize / 1024 / 1024);
	size_t totalpages = totalmemsize / PAGE_SIZE;
	printf("Total handled size %lu pages (%lu MB)\n", totalpages, totalmemsize / 1024 / 1024);
	size_t bitmapsize = totalpages / 8;
	printf("Bitmap will use %lu pages (%lu KB)\n", bitmapsize / PAGE_SIZE, bitmapsize / 1024);

	// find where to put the bitmap in	

	
	for(size_t i = 0; i < memmap_req.response->entry_count; ++i){
		
		struct limine_memmap_entry *current = memmap_req.response->entries[i];
		
		if(!current->type == LIMINE_MEMMAP_USABLE) continue;

		if(current->length < bitmapsize) continue;
		
		bitmap = (size_t*)current->base;
		
		break;

	}

	if(!bitmap)
		_panic("No available memory for bitmap", 0);

	printf("Bitmap at %p\n", bitmap);
	
	// mark needed entries as used now
	
	printf("Ranges: \n");
	
	for(size_t i = 0; i < memmap_req.response->entry_count; ++i){
		
		struct limine_memmap_entry *current = memmap_req.response->entries[i];
		
	
		printf("\033[93mP: %016p\033[0m -> \033[92mL: %016p\033[0m \033[94mT:%02lu", current->base,  current->base + current->length, current->type);

		if(i % 3 == 2) printf("\n\033[0m");

		if(current->type == LIMINE_MEMMAP_USABLE) continue;
		
		pmm_setused((void*)current->base, current->length / PAGE_SIZE + 1);
		



	}
	
	printf("\n");

	// mark the bitmap itself as used
	
	bitmappages = totalmemsize / PAGE_SIZE + 1;

	pmm_setused(bitmap, bitmappages);

}
