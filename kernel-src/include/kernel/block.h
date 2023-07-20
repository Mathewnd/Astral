#ifndef _BLOCK_H
#define _BLOCK_H

#include <stddef.h>
#include <stdint.h>

#define BLOCK_TYPE_DISK 0
#define BLOCK_TYPE_PART 1

typedef struct {
	void *private;
	int type;
	uintmax_t lbaoffset;
	size_t blockcapacity;
	size_t blocksize;
	int (*write)(void *private, void *buffer, uintmax_t lba, size_t count);
	int (*read)(void *private, void *buffer, uintmax_t lba, size_t count);
} blockdesc_t;

void block_register(blockdesc_t *desc, char *name);
void block_init();

#endif
