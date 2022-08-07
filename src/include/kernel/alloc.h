#ifndef _ALLOC_H_INCLUDE
#define _ALLOC_H_INCLUDE

#include <stddef.h>

void* alloc(size_t);
void  free(void*);
void* realloc(void*, size_t);

void alloc_init();

#endif
