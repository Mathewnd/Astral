#ifndef _ALLOC_H_INCLUDE
#define _ALLOC_H_INCLUDE

#include <stddef.h>

void* alloc(size_t);
void  free(void*);

void alloc_init();

#endif
