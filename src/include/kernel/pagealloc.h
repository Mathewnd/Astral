#ifndef _PAGEALLOC_H_INCLUDE
#define _PAGEALLOC_H_INCLUDE

#include <stddef.h>
#include <stdbool.h>

void* pageallocator_alloc(size_t size);
bool pageallocator_free(void* addr);
void* pageallocator_realloc(void* addr, size_t size, bool* nonexistant);

#endif
