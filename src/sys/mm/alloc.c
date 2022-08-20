#include <kernel/alloc.h>
#include <kernel/slab.h>
#include <string.h>
#include <kernel/pagealloc.h>

void* alloc(size_t size){
	if(size > 256) // slab implementation max size
		return pageallocator_alloc(size);
	else
		return slab_alloc(size);
}

void free(void* addr){
	if(pageallocator_free(addr))
		slab_free(addr);
}

void alloc_init(){
	slab_init();
}

void* realloc(void* addr, size_t size){
	
	bool nonexistant;

	void* newaddr = pageallocator_realloc(addr, size, &nonexistant);
	
	if(newaddr)
		return newaddr;

	if(nonexistant == 0)
		return NULL;


	size_t slabsize = slab_getentrysize(addr);

	newaddr = alloc(size);

	if(!newaddr)
		return NULL;
	
	memcpy(newaddr, addr, slabsize);

	slab_free(addr);
	
	return newaddr;

}
