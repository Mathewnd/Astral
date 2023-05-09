#ifndef _ALLOC_H
#define _ALLOC_H

#include <stddef.h>

void alloc_init();
void *alloc(size_t s);
void *realloc(void *addr, size_t s);
void free(void *addr);

#endif
