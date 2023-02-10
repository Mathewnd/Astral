#ifndef _BLOCK_H_INCLUDE
#define _BLOCK_H_INCLUDE

#include <stddef.h>
#include <stdint.h>

typedef struct{
	int (*read)(void* internal, void* buffer, uintmax_t lba, size_t count); 
} blockcalls_t;

typedef struct{
	blockcalls_t calls;
	void* internal;
	size_t blocksize, capacity;
} blockdesc_t;

int block_registernew(blockdesc_t* desc, char* name);

#endif
