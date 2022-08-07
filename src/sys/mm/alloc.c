#include <kernel/alloc.h>
#include <kernel/slab.h>
#include <string.h>

void* alloc(size_t size){
	return slab_alloc(size);
}

void free(void* addr){
	slab_free(addr);
}

void alloc_init(){
	slab_init();
}

void* realloc(void* addr, size_t size){
	void* tmp = alloc(size);
	if(!tmp)
		return NULL;
	
	memcpy(tmp, addr, size);

	free(addr);
	
	return tmp;

}
