#include <kernel/pmm.h>
#include <arch/panic.h>
#include <limine.h>
#include <stdio.h>
#include <string.h>
#include <kernel/spinlock.h>

#define PAGE_SIZE 4096

#define STATE_USED 1
#define STATE_FREE 0

void* limine_hhdm_offset;

volatile struct limine_hhdm_request hhdm_req = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

volatile struct limine_memmap_request memmap_req = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

size_t bitmappages;
void*   bitmaptop;
size_t* bitmap = 0;
size_t usablememsize = 0;
size_t totalmemsize  = 0;
void* lastfree = PAGE_SIZE;
void* pmm_usabletop;
int lock = 0;

static int getstate(void* addr){
	if(addr >= limine_hhdm_offset) addr -= limine_hhdm_offset;
	if(addr > bitmaptop) return STATE_USED;
	
	size_t page = (size_t)addr / PAGE_SIZE;

	// find the exact size_t sized chunk that the entry resides in	
	size_t chunk = page / 8 / sizeof(size_t);
	
	// where inside the size_t is it

	size_t offset = page % (sizeof(size_t)*8);

	return bitmap[chunk] & ((size_t)1 << offset) ? 1 : 0;// the cast is here so the compiler doesn't optimise it as a 32 constant

}

static void setstate(void* addr, size_t state){
	if(addr >= limine_hhdm_offset) addr -= limine_hhdm_offset;
	if(addr > bitmaptop) return;

	size_t page = (size_t)addr / PAGE_SIZE;

	// find the exact size_t sized chunk that the entry resides in	
	size_t chunk = page / 8 / sizeof(size_t);
	
	// where inside the size_t is it

	size_t offset = page % (sizeof(size_t)*8);

	size_t entry = bitmap[chunk];
	entry &= ~((size_t)1 << offset); // the cast is here so the compiler doesn't optimise it as a 32 constant
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

void pmm_free(void* addr, size_t count){
	if(count == 0) return;
	spinlock_acquire(&lock);
	
	for(size_t i = 0; i < count; ++i)	
		setstate(addr + PAGE_SIZE*i, STATE_FREE);
	
	lastfree = addr < lastfree ? addr : lastfree;

	spinlock_release(&lock);
}



void* pmm_alloc(size_t count){
	if(count == 0) return NULL;
	spinlock_acquire(&lock);
	
	void* addr = lastfree;
	


	for(; addr < bitmaptop; addr += PAGE_SIZE){
		
		if(getstate(addr) == STATE_USED)
			continue;
		
		void* base = addr;
		for(size_t i = 0; i < count; ++i){
			if(getstate(base) == STATE_USED) break;
			base += PAGE_SIZE;
		}
		
		// if the loop was successful, base will point to a free page

		if(getstate(base) == STATE_FREE)
			break;
		
		

		// nope, skip the entire thing
		addr = base;
	}
	

	if(addr >= bitmaptop){
		spinlock_release(&lock);
		return NULL;
	}
	
	for(size_t i = 0; i < count; ++i) setstate(addr + i * PAGE_SIZE, STATE_USED);


	spinlock_release(&lock);

	return addr;

}

void* pmm_hhdmalloc(size_t count){
	void* alloc = pmm_alloc(count);
	if(!alloc) return NULL;
	return alloc + (size_t)limine_hhdm_offset;
}

char* typesstr[] = {
	"Usable",
	"Reserved",
	"ACPI Reclaimable",
	"ACPI NVS",
	"Bad mememory",
	"Bootloader reclaimable",
	"Kernel/Module",
	"Framebuffer"
};

void pmm_init(){
	
	if(!hhdm_req.response)
		_panic("No hhdm request response", 0);
	if(!memmap_req.response)
		_panic("No memmap request response", 0);
	
	limine_hhdm_offset = hhdm_req.response->offset;
	
	printf("Limine hhdm at %p\n", limine_hhdm_offset);

	// find usable memory size
	
	struct limine_memmap_entry *lastusable;

	for(size_t i = 0; i < memmap_req.response->entry_count; ++i){
		struct limine_memmap_entry *current = memmap_req.response->entries[i];
		
		totalmemsize += current->length;

		if(!current->type == LIMINE_MEMMAP_USABLE)
			continue;

		usablememsize += current->length;
		void* top = current->base + current->length;
		bitmaptop = top > bitmaptop ? top : bitmaptop;
	}

	
	printf("Memory Size: %lu pages (%lu MB)\n", usablememsize / PAGE_SIZE, usablememsize / 1024 / 1024);
	size_t totalpages = totalmemsize / PAGE_SIZE;
	printf("Total handled size %lu pages (%lu MB)\n", totalpages, totalmemsize / 1024 / 1024);
	size_t bitmapsize = (size_t)bitmaptop / PAGE_SIZE / 8;
	printf("Bitmap will use %lu pages (%lu KB)\n", bitmapsize / PAGE_SIZE, bitmapsize / 1024);

	// find where to put the bitmap in	

	
	for(size_t i = 0; i < memmap_req.response->entry_count; ++i){
		
		struct limine_memmap_entry *current = memmap_req.response->entries[i];
		
		if(!current->type == LIMINE_MEMMAP_USABLE) continue;

		// don't put it in low memory

		if(current->length < bitmapsize || current->base < 0x100000) continue;
		
		bitmap = (size_t*)current->base;
		
		break;

	}

	if(!bitmap)
		_panic("No available memory for bitmap", 0);

	printf("Bitmap at %p\n", bitmap);

	memset(bitmap, 0, bitmapsize);

	// mark needed entries as used now
	
	printf("Ranges: \n");
	
	for(size_t i = 0; i < memmap_req.response->entry_count; ++i){
		
		struct limine_memmap_entry *current = memmap_req.response->entries[i];
		
	
		printf("\033[93mA: %016p\033[0m -> \033[93m%016p \033[94mT:%s  ", current->base,  current->base + current->length, typesstr[current->type]);
		

		for(size_t i = 0; i < 22 - strlen(typesstr[current->type]); ++i)
			printf(" ");

		if(i % 2) printf("\n\033[0m");

		if(current->type == LIMINE_MEMMAP_USABLE) continue;
		
		pmm_setused((void*)current->base, current->length / PAGE_SIZE + 1);

	}

	printf("\033[0m\n");

	// mark the bitmap itself as used
	
	bitmappages = (size_t)bitmaptop / PAGE_SIZE + 1;

	pmm_setused(bitmap, bitmapsize / 4096);

	pmm_usabletop = bitmaptop;

}
