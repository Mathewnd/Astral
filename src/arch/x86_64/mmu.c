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
#include <arch/idt.h>
#include <arch/smp.h>
#include <kernel/semaphore.h>

volatile struct limine_kernel_address_request kaddrreq = {
	.id = LIMINE_KERNEL_ADDRESS_REQUEST,
	.revision = 0
};

static void changeentry(uint64_t* entry, void* paddr, uint64_t flags){
	if(entry < limine_hhdm_offset) entry = (size_t)entry + (size_t)limine_hhdm_offset;
	*entry = (uint64_t)paddr | flags;
}

#define BIG_PAGESIZE (512*4096)

#define PTR_FLAGS ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE | ARCH_MMU_MAP_USER | ARCH_MMU_MAP_NOEXEC

void arch_mmu_switchcontext(arch_mmu_tableptr context){
	asm("mov %%rax, %%cr3" : : "a"(context) : "memory");
}

void* inv = NULL;
int invlock;
semaphore_t sem;

void arch_mmu_invalidateipi(){
	asm("invlpg (%%rax)" : : "a"(inv));
	sem_signal(&sem);
}

static void invalidate(void* addr){
	spinlock_acquire(&invlock);
	asm("invlpg (%%rax)" : : "a"(addr));
	sem.count = -arch_smp_cpucount();
	if(!sem.count) return;
	sem.count += 2;
	inv = addr;
	arch_smp_sendipi(0, VECTOR_MMUINVAL, IPI_CPU_ALLBUTSELF);
	while(!sem_tryacquire(&sem)) asm("pause");
	spinlock_release(&invlock);
}


static uint64_t* next(uint64_t* table, size_t offset){
	
	table = (uint64_t)table & ~((uint64_t)0xFFF);
	
	if(table < limine_hhdm_offset){
		table = (uint64_t)table & ~(1 << (uint64_t)63); // remove NX
		table = (uint64_t)table + (uint64_t)limine_hhdm_offset;
	}
	
	return table[offset] & ~((uint64_t)0xFFF);

}

static bool setpage(arch_mmu_tableptr context, void* vaddr, uint64_t entry){
	uint64_t addr = (uint64_t)vaddr;
	size_t pdpt = (addr >> 39) & 0b111111111;
	size_t pd   = (addr >> 30) & 0b111111111;
	size_t pt   = (addr >> 21) & 0b111111111;
	size_t page = (addr >> 12) & 0b111111111;
	

	uint64_t* pdptaddr = next(context ,pdpt);

	if(!pdptaddr){
		pdptaddr = pmm_alloc(1);
		if(!pdptaddr) return false;
		changeentry(&context[pdpt], pdptaddr, PTR_FLAGS);
		memset((void*)pdptaddr + (size_t)limine_hhdm_offset, 0, PAGE_SIZE);
	}

	uint64_t* pdaddr = next(pdptaddr, pd);

	if(!pdaddr){
		pdaddr = pmm_alloc(1);
		if(!pdaddr) return false;
		changeentry(&pdptaddr[pd],  pdaddr, PTR_FLAGS);
		memset((void*)pdaddr + (size_t)limine_hhdm_offset, 0, PAGE_SIZE);
	}
	
	uint64_t* ptaddr = next(pdaddr, pt);

	if(!ptaddr){
		ptaddr = pmm_alloc(1);
		if(!ptaddr) return false;
		changeentry(&pdaddr[pt],  ptaddr, PTR_FLAGS);
		memset((void*)ptaddr + (size_t)limine_hhdm_offset, 0, PAGE_SIZE);
	}

	ptaddr = (void*)ptaddr + (size_t)limine_hhdm_offset;

	ptaddr[page] = entry;

	return true;

}

static uint64_t getmapping(arch_mmu_tableptr context, void* vaddr){
	uint64_t addr = (uint64_t)vaddr;
        size_t pdpt = (addr >> 39) & 0b111111111;
        size_t pd   = (addr >> 30) & 0b111111111;
        size_t pt   = (addr >> 21) & 0b111111111;
        size_t page = (addr >> 12) & 0b111111111;
	
	uint64_t* table = next(context, pdpt);
	
	if(!table) return 0;
	
	table = next(table, pd);

	if(!table) return 0;

	table = next(table, pt);

	if(!table) return 0;

	table = next(table, page);

	return table;

}

bool arch_mmu_ismapped(arch_mmu_tableptr context, void* vaddr){
	
	uint64_t mapping = getmapping(context, vaddr);

	return mapping != 0;
	
}

void* arch_mmu_getphysicaladdr(arch_mmu_tableptr context, void* addr){
	uint64_t paddr = getmapping(context, addr);
	paddr &= ~((uint64_t)0xFFF);
	paddr &= ~(uint64_t)ARCH_MMU_MAP_NOEXEC;
	return paddr;
}


static void destroy(uint64_t* table, int depth){
	
	for(size_t i = 0; i < 512; ++i){

		if(depth == 0 && i == 256)
			break;
		
		uint64_t paddr = table[i];

		if(!paddr)
			continue;

		paddr &= ~((uint64_t)0xFFF);
		paddr &= ~(uint64_t)ARCH_MMU_MAP_NOEXEC;
		
		if(depth < 3)
			destroy(paddr+limine_hhdm_offset, depth+1);

		pmm_free(paddr, 1);

	}

}

void arch_mmu_destroy(arch_mmu_tableptr context){
	if(context < limine_hhdm_offset)
		context = (uint8_t*)context + (uintptr_t)limine_hhdm_offset;
	destroy(context, 0);
}

bool arch_mmu_isaccessed(arch_mmu_tableptr context, void* addr){
	return (getmapping(context, addr) & ARCH_MMU_MAP_ACCESSED) != 0;
}

void arch_mmu_changeflags(arch_mmu_tableptr context, void* addr, size_t flags, size_t count){
	
	for(size_t i = 0; i < count; ++i){
		uint64_t mapping = getmapping(context, addr);
		if(!mapping)
			continue;

		mapping &= ~((uint64_t)0xFFF); // get addr
		mapping &= ~((uint64_t)1 << 63);
		mapping |= flags;
		setpage(context, addr, mapping);
		invalidate(addr);
		addr += PAGE_SIZE;
	}

	
}

int arch_mmu_map(arch_mmu_tableptr context, void* paddr, void* vaddr, size_t flags){
	uint64_t entry;
	changeentry(&entry, paddr, flags);

	int ret = setpage(context, vaddr, entry);
	
	return ret;

}

void arch_mmu_unmap(arch_mmu_tableptr context, void* vaddr){
	
	if(!getmapping(context, vaddr)) return;

	arch_mmu_map(context, 0, vaddr, 0);
	invalidate(vaddr);
	
}

static arch_mmu_tableptr context; //boostrap context; also holds the kernel page tables

arch_mmu_tableptr arch_mmu_newcontext(){
	
	arch_mmu_tableptr newcontext = pmm_alloc(1);
	if(!newcontext)
		return NULL;

	memcpy((void*)newcontext + (size_t)limine_hhdm_offset, (void*)context + (uintptr_t)limine_hhdm_offset, PAGE_SIZE);

	memset((void*)newcontext + (size_t)limine_hhdm_offset, 0, PAGE_SIZE / 2);

	return newcontext;

}

void arch_mmu_init(){
	
	context = pmm_alloc(1);
	if(!context) _panic ("Out of memory", 0);
	memset(context, 0, PAGE_SIZE);

	// make sure to have all the kernel memory point to something so they
	// can be all equal
	
	for(size_t i = 256; i < 512; ++i){
		void* addr = pmm_alloc(1);
		if(!addr) _panic("Out of memory", 0);
		memset(addr, 0, PAGE_SIZE);
		size_t entry;
		changeentry(&entry, addr, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE | ARCH_MMU_MAP_NOEXEC);
		context[i] = entry;
	}

	// map hhdm and lower memory (limine terminal needs this. later we'll
	// use another layout and only switch to it on a limine tty call)
	
	extern volatile struct limine_memmap_request memmap_req;
	struct limine_memmap_entry **entries = memmap_req.response->entries;
	size_t count = memmap_req.response->entry_count;
		
	for(size_t entry = 0; entry < count; ++entry){
		void* addr = entries[entry]->base;
		
		for(size_t pageoffset = 0; pageoffset < entries[entry]->length / PAGE_SIZE; ++pageoffset){
			uint64_t entry;

			changeentry(&entry, addr + pageoffset*PAGE_SIZE, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE);

			setpage(context, addr + pageoffset*PAGE_SIZE, entry);
			setpage(context, addr + pageoffset*PAGE_SIZE + (size_t)limine_hhdm_offset, entry);
			
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
	

	arch_mmu_switchcontext(context);
	arch_getcls()->context->context = context;

	printf("In bootstrap context\n");

}

void arch_mmu_apinit(){
	
	arch_mmu_switchcontext(context);

	arch_getcls()->context = pmm_hhdmalloc(1);

	arch_getcls()->context->context = context;

}
