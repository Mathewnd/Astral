#ifndef _SLAB_H_INCLUDE
#define _SLAB_H_INCLUDE


#include <stddef.h>

void* slab_alloc(size_t);
void  slab_free(void*);
size_t slab_getentrysize(void*);
void slab_init();

#endif
