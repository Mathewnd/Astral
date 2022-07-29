#include <kernel/alloc.h>
#include <kernel/slab.h>

void* alloc(size_t size){
	return slab_alloc(size);
}

void free(void* addr){
	slab_free(addr);
}

void alloc_init(){
	slab_init();
}
