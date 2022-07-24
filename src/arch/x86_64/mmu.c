#include <arch/mmu.h>
#include <arch/cls.h>
#include <kernel/pmm.h>
#include <stdint.h>
#include <stdio.h>
#include <limine.h>
#include <arch/panic.h>
#include <stdbool.h>
#include <string.h>
#include <arch/cls.h>

static volatile struct limine_kernel_address_request kaddrreq = {
	.id = LIMINE_KERNEL_ADDRESS_REQUEST,
	.revision = 0
};

static void changeentry(uint64_t* entry, void* paddr, uint64_t flags){
	if(entry < limine_hhdm_offset) entry = (size_t)entry + (size_t)limine_hhdm_offset;
	*entry = (uint64_t)paddr | flags;
}

#define BIG_PAGESIZE (512*4096)

#define PTR_FLAGS ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE | ARCH_MMU_MAP_USER | ARCH_MMU_MAP_NOEXEC

static void switchcontext(arch_mmu_tableptr context){
	asm("mov %%rax, %%cr3" : : "a"(context));
}

static void invalidate(void* addr){
	asm("invlpg (%%rax)" : : "a"(addr));
}

static bool setpage(arch_mmu_tableptr context, void* vaddr, uint64_t entry){
	uint64_t addr = (uint64_t)vaddr;
	size_t pdpt = (addr >> 39) & 0b111111111;
	size_t pd   = (addr >> 30) & 0b111111111;
	size_t pt   = (addr >> 21) & 0b111111111;
	size_t page = (addr >> 12) & 0b111111111;

	context = (void*)context + (size_t)limine_hhdm_offset;

	uint64_t* pdptaddr = context[pdpt] & ~0xFFF;

	// TODO unallocate in case of failure	
	
	if(!pdptaddr){
		pdptaddr = pmm_alloc(1);
		changeentry(&context[pdpt], pdptaddr, PTR_FLAGS);
		memset((void*)pdptaddr + (size_t)limine_hhdm_offset, 0, PAGE_SIZE);
	}

	pdptaddr = (void*)pdptaddr + (size_t)limine_hhdm_offset;

	uint64_t* pdaddr = pdptaddr[pd] & ~0xFFF;
	
	if(!pdaddr){
		pdaddr = pmm_alloc(1);
		changeentry(&pdptaddr[pd],  pdaddr, PTR_FLAGS);
		memset((void*)pdaddr + (size_t)limine_hhdm_offset, 0, PAGE_SIZE);
	}
	
	pdaddr = (void*)pdaddr + (size_t)limine_hhdm_offset;

	uint64_t* ptaddr = pdaddr[pt]& ~0xFFF;



	if(!ptaddr){
		ptaddr = pmm_alloc(1);
		changeentry(&pdaddr[pt],  ptaddr, PTR_FLAGS);
		memset((void*)ptaddr + (size_t)limine_hhdm_offset, 0, PAGE_SIZE);
	}

	ptaddr = (void*)ptaddr + (size_t)limine_hhdm_offset;

	ptaddr[page] = entry;

	return true;

}



int arch_mmu_map(arch_mmu_tableptr context, void* paddr, void* vaddr, size_t flags){
	uint64_t entry;
	changeentry(&entry, paddr, flags);

	int ret = setpage(context, vaddr, entry);
	
	if(ret) invalidate(vaddr);

	return ret;

}

// TODO unmap

void arch_mmu_init(){
	
	arch_mmu_tableptr context;
	
	context = pmm_alloc(1);
	memset(context, 0, PAGE_SIZE);
	if(!context) _panic ("Out of memory", 0);

	// TODO use big pages (huge pages if processor supports it) for this
	
	// map hhdm and lower memory (limine terminal needs this. later we'll
	// use another layout and only switch to it on a limine tty call)
	
	extern volatile struct limine_memmap_request memmap_req;
	struct limine_memmap_entry **entries = memmap_req.response->entries;
	size_t count = memmap_req.response->entry_count;
		
	for(size_t entry = 0; entry < count; ++entry){
		void* addr = entries[entry]->base;
		
		for(size_t offset = 0; offset < entries[entry]->length; offset += PAGE_SIZE){
			uint64_t entry;

			changeentry(&entry, addr + offset, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE);

			setpage(context, addr + offset, entry);
			setpage(context, addr + offset + (size_t)limine_hhdm_offset, entry);
			
		}

	}

	// map the kernel itself now
	
	
	// kernel linking is:
	// text -> rodata -> data
	
	extern int _text_start, _text_end, _rodata_start, _rodata_end, _data_start, _data_end;

	void* textstart = &_text_start;
	void* textend   = &_text_end;
	void* rodatastart = &_rodata_start;
	void* rodataend   = &_rodata_end;
	void* datastart   = &_data_start;
	void* dataend     = &_data_end;

	printf("TEXT START: %p TEXT END: %p\nRODATA START: %p RODATA END: %p\nDATA START: %p DATA END: %p\n", textstart, textend, rodatastart, rodataend, datastart, dataend);
	
	if(!kaddrreq.response) _panic("Kernel physical base not passed by bootloader", 0);
	
	void* kphysical = kaddrreq.response->physical_base;

	printf("Kernel physical base: %p\n", kphysical);

	for(void* addr = textstart; addr < textend; addr += PAGE_SIZE){
		uint64_t entry;
		changeentry(&entry, kphysical, ARCH_MMU_MAP_READ);
		setpage(context, addr, entry);

		kphysical += PAGE_SIZE;
	}

	for(void* addr = rodatastart; addr < rodataend; addr += PAGE_SIZE){
		uint64_t entry;
		changeentry(&entry, kphysical, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_NOEXEC);
		setpage(context, addr, entry);

		kphysical += PAGE_SIZE;
	}

	for(void* addr = datastart; addr < dataend; addr += PAGE_SIZE){
		uint64_t entry;
		changeentry(&entry, kphysical, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE | ARCH_MMU_MAP_NOEXEC);
		setpage(context, addr, entry);

		kphysical += PAGE_SIZE;
	}
	

	switchcontext(context);
	
	printf("In bootstrap context\n");

}
