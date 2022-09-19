#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <arch/panic.h>
#include <string.h>
#include <arch/spinlock.h>
#include <stdio.h>
#include <stdbool.h>
#include <limine.h>
#include <arch/cls.h>
#include <kernel/alloc.h>

vmm_cache* caches;
int klock;
vmm_mapping *kmapstart;

static vmm_cache* newcache(){
	vmm_cache* cache = pmm_hhdmalloc(1);
	if(!cache) return NULL;
	memset(cache, 0, sizeof(vmm_cache));
	cache->header.freecount = VMM_CACHE_ENTRY_COUNT;
	return cache;
}

static void freecache(vmm_cache* cache){
	pmm_hhdmfree(cache, 1);
}

static void freeentry(vmm_mapping* entry){
	spinlock_acquire(&entry->cache->lock);
	
	vmm_cacheheader* cache = entry->cache;

	++entry->cache->freecount;
	memset(entry, 0, sizeof(vmm_mapping));

	size_t n = ((size_t)entry - (size_t)cache + sizeof(vmm_cacheheader)) / sizeof(vmm_mapping);
	
	if(n < cache->firstfree)
		cache->firstfree = n;

	spinlock_release(&cache->lock);
}

static vmm_mapping* allocentry(vmm_cache* cache){
	
	vmm_mapping* found = NULL;

	spinlock_acquire(&cache->header.lock);

	if(cache->header.freecount == 0){
		spinlock_release(&cache->header.lock);
		return NULL;
	}
	
	for(size_t i = cache->header.firstfree; i < VMM_CACHE_ENTRY_COUNT; ++i){
		if(!cache->mappings[i].cache){
			found = &cache->mappings[i];
			found->cache = cache;
			cache->header.firstfree = i;
			goto done;
		}
	}

	done:
	
	--cache->header.freecount;
	spinlock_release(&cache->header.lock);
	
	return found;
}

static vmm_mapping* allocatefirst(){

	vmm_mapping *map = NULL;
	vmm_cache   *cache = caches;

	map = allocentry(cache);
	
	while(cache->header.next && map == NULL){
		cache = cache->header.next;
		map = allocentry(cache);
	}
	
	spinlock_acquire(&cache->header.lock);

	if(!map){
		// try allocating new cache
		vmm_cache* new = newcache();
		if(!new) goto done;
		map = allocentry(new);
		cache->header.next = new;
	}
	
	done:

	spinlock_release(&cache->header.lock);


	return map;

}

static char* debugnames[] = {
	"FREE",
	"ANON",
	"FILE"
};

static void debug_dumpusermappings(){
	vmm_mapping* map = arch_getcls()->context->userstart;
	
	printf("User mappings:\n");

	while(map != NULL){
		printf("START: %016p END: %016p MMUFLAGS: %016p DATA: %016p OFFSET: %lu TYPE: %s\n", map->start, map->end, map->mmuflags, map->data, map->offset, debugnames[map->type]);
		map = map->next;
	}

}
static void debug_dumpkernelmappings(){
	vmm_mapping* map = kmapstart;
	
	printf("Kernel mappings:\n");

	while(map != NULL){
		printf("START: %016p END: %016p MMUFLAGS: %016p DATA: %016p OFFSET: %lu TYPE: %s\n", map->start, map->end, map->mmuflags, map->data, map->offset, debugnames[map->type]);
		map = map->next;
	}

}

static void fragcheck(vmm_mapping* map){
	if(map->type == VMM_TYPE_FILE)
		return;
	
	if(map->prev && map->prev->type == map->type && map->prev->mmuflags == map->mmuflags){
		vmm_mapping *prev = map->prev;
		prev->next = map->next;
		prev->end = map->end;
		if(map->next)
			map->next->prev = prev;
		freeentry(map);
		map = prev;
	}

	if(map->next && map->next->type == map->type && map->next->mmuflags == map->mmuflags){
		vmm_mapping* next = map->next;
		map->next = next->next;
		map->end = next->end;
		if(next->next)
			next->next->prev = map;
		freeentry(next);
	}
	
}

// this could be rewritten later

static bool setmap(vmm_mapping** mapstart, void* addr, size_t pagec, size_t mmuflags, size_t type, void* data, size_t offset){

	vmm_mapping* map = *mapstart;
	while(map && addr > map->end)
		map = map->next;

	if(!map) return false;

	vmm_mapping* newmap = allocatefirst();

	if(!newmap) return false;


	newmap->start = addr;
	newmap->end = addr + pagec*PAGE_SIZE-1;
	newmap->mmuflags = mmuflags;
	newmap->type = type;
	newmap->data = data;
	newmap->offset = offset;
	
	// spans multiple entries?
	if(newmap->end > map->end){
		vmm_mapping* nextmap = map->next;
		while(nextmap && newmap->end < nextmap->start)
			nextmap = nextmap->next;

		if(!nextmap){
			freeentry(newmap);
			return false;
		}
		
		// unallocate entries inbetween
		
		vmm_mapping* loopmap = map->next;

		while(loopmap != nextmap){
			loopmap = loopmap->next;
			freeentry(loopmap->prev);
		}
		
		map->end = newmap->start-1;
		
		if(nextmap->type == VMM_TYPE_FILE)
			nextmap->offset += newmap->end - nextmap->start + 1;
		nextmap->start = newmap->end+1;
		
		map->next = newmap;
		newmap->prev = map;
		newmap->next = nextmap;
		nextmap->prev = newmap;
		
		if(map->start >= map->end){
			newmap->prev = map->prev;
			freeentry(map);
			if(map->prev == NULL)
				*mapstart = newmap;
		}
		
		if(nextmap->start >= nextmap->end){
			newmap->next = nextmap->next;
			freeentry(nextmap);
		}
		
		fragcheck(newmap);
		
		return true;

	}

	// do we need to split?
	
	if(map->start != newmap->start && map->end != newmap->end){
		vmm_mapping* splitmap = allocatefirst();
		if(!splitmap){
			freeentry(newmap);
			return false;
		}
		
		splitmap->next = map->next;
		splitmap->prev = newmap;
		splitmap->end = map->end;
		splitmap->start = newmap->end+1;
		splitmap->type = map->type;
		if(splitmap->type == VMM_TYPE_FILE)
			splitmap->offset = map->offset + splitmap->start - map->start;
		splitmap->data = map->data;
		
		map->next = newmap;
		map->end  = newmap->start-1;
		
		newmap->prev = map;
		newmap->next = splitmap;

		return true;
	}
	
	if(map->start == newmap->start){
		//start
		newmap->next = map;
		newmap->prev = map->prev;
		if(newmap->prev){
			vmm_mapping* tmp = newmap->prev;
			tmp->next = newmap;
		}
		else
			*mapstart = newmap;


		map->start = newmap->end+1;
		if(map->type == VMM_TYPE_FILE)
			map->offset += newmap->end - newmap->start + 1;
		map->prev = newmap;

		if(map->start >= map->end){
			newmap->next = map->next;
			freeentry(map);
		}
	}
	else{
		// end
		newmap->next = map->next;
		newmap->prev = map;

		if(newmap->next){
			vmm_mapping* tmp = newmap->next;
			tmp->prev = newmap;
		}

		map->end = newmap->start-1;
		map->next = newmap;

		if(map->start >= map->end){
			newmap->prev = map->prev;
			freeentry(map);
		}

	}


	fragcheck(newmap);
	return true;

}

static vmm_mapping* findmappingfromaddr(vmm_mapping* map, void* addr){
	while(map && !(addr >= map->start && addr <= map->end))
		map = map->next;
		
	return map;

}

static void* findfirstfreearea(vmm_mapping* map, size_t pagec){
	while(map && map->end < map->start + pagec*PAGE_SIZE-1 || map->type != VMM_TYPE_FREE)
		map = map->next;

	if(map) return map->start;
	
	return NULL;
}


static bool unmap(vmm_mapping** mapstart, void* addr, size_t pagec){
	if(pagec == 0) return true;
	vmm_mapping *map = *mapstart;
	void* savedaddr = addr;
	arch_mmu_tableptr context = arch_getcls()->context->context;
	
	for(size_t page = 0; page < pagec; ++page, addr += PAGE_SIZE){
		map = findmappingfromaddr(map, addr);
		switch(map->type){
			case VMM_TYPE_ANON:
				if(arch_mmu_isaccessed(context, addr)){
					void* paddr = arch_mmu_getphysicaladdr(context, addr);
					pmm_free(paddr, 1);
					arch_mmu_unmap(context, addr);
				}		
				break;

			case VMM_TYPE_FILE:
				_panic("File mappings not supported!", 0);

			default:
				continue;

		}
	}
	

	setmap(mapstart, savedaddr, pagec, 0, VMM_TYPE_FREE, 0, 0);

	return true;

}

static void getcontextinfo(void* addr, int** lock, vmm_mapping*** start){
	
	if(addr >= KERNEL_SPACE_START){
		*lock = &klock;
		*start = &kmapstart;
		return;
	}

	vmm_context *context = arch_getcls()->context;
	
	*lock = &context->lock;
	*start = &context->userstart;
	
}

int vmm_fork(vmm_context* oldctx, vmm_context* newctx){
	
	// TODO make this copy on write
	
	spinlock_acquire(&oldctx->lock);
	spinlock_acquire(&newctx->lock);

	vmm_mapping* mapping = oldctx->userstart;

	while(mapping){
		
		if(mapping->type == VMM_TYPE_FILE)
			_panic("File mappings not supported yet!", NULL);

		size_t pagesize =((uintptr_t)mapping->end - (uintptr_t)mapping->start + 1) / PAGE_SIZE; 

		if(!setmap(&newctx->userstart, mapping->start, pagesize, mapping->mmuflags, mapping->type, mapping->data, mapping->offset))
				
			goto _fail;
		


		if(mapping->type == VMM_TYPE_ANON){
			
			
			for(uintmax_t page = 0; page < pagesize; ++page){
				void* pageaddr = mapping->start + page*PAGE_SIZE;

				if(arch_mmu_ismapped(oldctx->context, pageaddr) == false)
					continue;

				void* newp = pmm_alloc(1);

				if(!newp)
					goto _fail;

				if(!arch_mmu_map(newctx->context, newp, pageaddr, mapping->mmuflags))
					goto _fail;
				

				void* oldp = arch_mmu_getphysicaladdr(oldctx->context, pageaddr);

				oldp += (uintptr_t)limine_hhdm_offset;
				newp += (uintptr_t)limine_hhdm_offset;

				memcpy(newp, oldp, PAGE_SIZE);

				
			}
			

		}

		
		mapping = mapping->next;

	}
	

	spinlock_release(&oldctx->lock);
	spinlock_release(&newctx->lock);
	return 0;

	_fail:
		spinlock_release(&oldctx->lock);
		spinlock_release(&newctx->lock);
		return ENOMEM;
	

}

void* vmm_alloc(size_t pagec, size_t mmuflags){
	spinlock_acquire(&klock);

	void* addr = findfirstfreearea(kmapstart, pagec);
	
	if(!addr){
		spinlock_release(&klock);
		return NULL;
	};

	

	if(!setmap(&kmapstart, addr, pagec, mmuflags, VMM_TYPE_ANON, 0, 0))
		addr = NULL;
	
	spinlock_release(&klock);

	return addr;
}

void* vmm_allocfrom(void* addr, size_t mmuflags, size_t pagec){
	
	int* lock;
	vmm_mapping** start;
	
	getcontextinfo(addr, &lock, &start);

	spinlock_acquire(lock);
	
	vmm_mapping* map = findmappingfromaddr(*start, addr);

	addr = findfirstfreearea(map, pagec);

	if(!addr)
		goto _done;
	
	if(!setmap(start, addr, pagec, mmuflags, VMM_TYPE_ANON, 0, 0))
		addr = NULL;
	
	_done:
	
	spinlock_release(lock);

	return addr;
	
}

bool vmm_unmap(void* addr, size_t pagec){
	
	int* lock;
	vmm_mapping** start;
	
	getcontextinfo(addr, &lock, &start);
	
	spinlock_acquire(lock);

	unmap(start, addr, pagec);

	spinlock_release(lock);

}

bool vmm_setused(void* addr, size_t pagec, size_t mmuflags){

	int* lock;
	vmm_mapping** start;
	
	getcontextinfo(addr, &lock, &start);
	
	spinlock_acquire(lock);

	bool result = setmap(start, addr, pagec, mmuflags, VMM_TYPE_ANON, 0, 0);
	
	spinlock_release(lock);
	return result;
}

bool vmm_setfree(void* addr, size_t pagec){
	int* lock;
	vmm_mapping** start;
	
	getcontextinfo(addr, &lock, &start);

	spinlock_acquire(lock);

	bool result = setmap(start, addr, pagec, 0, VMM_TYPE_FREE, 0, 0);
	
	spinlock_release(lock);
	return result;
}

bool vmm_map(void* paddr, void* vaddr, size_t pagec, size_t mmuflags){
	int* lock;
	vmm_mapping** start;
	
	getcontextinfo(vaddr, &lock, &start);

	spinlock_acquire(lock);
	bool result = setmap(start, vaddr, pagec, mmuflags, VMM_TYPE_ANON, 0, 0);

	for(size_t page = 0; page < pagec && result; ++page)
		result = arch_mmu_map(arch_getcls()->context->context, paddr + page*PAGE_SIZE, vaddr + page*PAGE_SIZE, mmuflags);

	spinlock_release(lock);
	return result;
}

bool vmm_allocnowat(void* addr, size_t mmuflags, size_t size){
	int* lock;
	vmm_mapping** start;
	bool result = true;
	
	addr = addr - ((uintptr_t)addr % PAGE_SIZE);

	getcontextinfo(addr, &lock, &start);
	
	size = size / PAGE_SIZE + (size % PAGE_SIZE ? 1 : 0);

	spinlock_acquire(lock);
	
	vmm_mapping* m = findmappingfromaddr(*start, addr);

	if(m->type != VMM_TYPE_FREE || (addr + size*PAGE_SIZE-1) > m->end){
		spinlock_release(lock);
		return false;
	}


	// XXX allocate 1 page at a time to reduce fragmentation

	void* paddr = pmm_alloc(size);
	if(!paddr)
		result = false;
	else
		result = setmap(start, addr, size, mmuflags, VMM_TYPE_ANON, 0, 0);

	for(size_t page = 0; page < size && result; ++page)
		result = arch_mmu_map(arch_getcls()->context->context, paddr + page*PAGE_SIZE, addr + page*PAGE_SIZE, mmuflags);
	

	spinlock_release(lock);

	return result;

}

// called by the page fault handler

bool vmm_dealwithrequest(void* addr, long error, bool user){
	
	if(user && addr > USER_SPACE_END)
		return false;

	int* lock;
	vmm_mapping** start = NULL;
	
	getcontextinfo(addr, &lock, &start);
	
	if(!start) return false;

	spinlock_acquire(lock);


	bool status;
	vmm_mapping* map = findmappingfromaddr(*start, addr);
	void* paddr;

	if((!map) || map->type == VMM_TYPE_FREE){
		status = false;
		goto done;
	}

	if(map->type == VMM_TYPE_FILE)
		_panic("File mappings are not supported (yet)", 0);

	size_t goodmmuflags = 1;

	if(error & ARCH_MMU_ERROR_WRITE)
		goodmmuflags |= ARCH_MMU_MAP_WRITE;
	
	if(error & ARCH_MMU_ERROR_USER)
		goodmmuflags |= ARCH_MMU_MAP_USER;
	
	if(error & ARCH_MMU_ERROR_INSTFETCH == 0)
		goodmmuflags |= ARCH_MMU_MAP_NOEXEC;


	if(goodmmuflags != (map->mmuflags & goodmmuflags)){
		status = false;
		goto done;
	}
	

	// allocate a page

	addr = (size_t)addr & ~(0xFFF);

	//printf("Demand paging %p\n", addr);
	
	

	cls_t* cls = arch_getcls();
	

	paddr = pmm_alloc(1);


	if(addr == NULL || arch_mmu_map(cls->context->context, paddr, addr, map->mmuflags) == false){
		status = false;	
		goto done;
	}
	
	status = true;
	
	memset(addr, 0, PAGE_SIZE);
	
	done:
	
	spinlock_release(lock);

	return status;

}

void vmm_switchcontext(vmm_context* context){
	if(arch_getcls()->thread)
		arch_getcls()->thread->ctx = context;
	arch_getcls()->context = context;
	arch_mmu_switchcontext(context->context);
}

vmm_context* vmm_newcontext(){

	vmm_context* context = alloc(sizeof(vmm_context));
	if(!context)
		return NULL;
	vmm_mapping* map = allocatefirst(caches);
	if(!map){
		free(context);
		return NULL;
	}

	context->context = arch_mmu_newcontext();
	if(!context->context){
		free(context);
		freeentry(map);
		return NULL;
	}

	map->start = USER_SPACE_START;
	map->end = USER_SPACE_END;

	context->userstart = map;

	return context;

}

void vmm_init(){
	printf("%lu mappings per cache\n", VMM_CACHE_ENTRY_COUNT);
	caches = newcache();
	if(!caches) _panic("Out of memory", 0);
	
	kmapstart = allocentry(caches);
	kmapstart->start = (void*)KERNEL_SPACE_START;
	kmapstart->end   = (void*)KERNEL_SPACE_END;

	
	// add the hhdm to the maps
	
	extern volatile struct limine_memmap_request memmap_req;
	size_t count = memmap_req.response->entry_count;
	struct limine_memmap_entry** entries = memmap_req.response->entries;
	
	for(size_t entry = 0; entry < count; ++entry){
		setmap(&kmapstart, (size_t)entries[entry]->base + (size_t)limine_hhdm_offset & ~(0xFFF), entries[entry]->length / PAGE_SIZE, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE, VMM_TYPE_ANON, 0, 0);
	}
	
	// now the kernel

        extern int _text_start, _text_end, _rodata_start, _rodata_end, _data_start, _data_end;

        void* textstart = &_text_start;
        void* textend   = &_text_end;
        void* rodatastart = &_rodata_start;
        void* rodataend   = &_rodata_end;
        void* datastart   = &_data_start;
        void* dataend     = &_data_end;
	
	setmap(&kmapstart, textstart, (textend - textstart) / PAGE_SIZE, ARCH_MMU_MAP_READ, VMM_TYPE_ANON, 0, 0);

	setmap(&kmapstart, rodatastart, (rodataend - rodatastart) / PAGE_SIZE, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_NOEXEC, VMM_TYPE_ANON, 0, 0);

	setmap(&kmapstart, datastart, (dataend - datastart) / PAGE_SIZE, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE | ARCH_MMU_MAP_NOEXEC, VMM_TYPE_ANON, 0, 0);

	debug_dumpkernelmappings();
		
	
	
}
