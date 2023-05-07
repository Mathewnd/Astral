#ifndef _SYSCALLS_H
#define _SYSCALLS_H

#include <stdint.h>
#include <arch/context.h>

typedef struct {
	uint64_t ret;
	uint64_t errno;
} syscallret_t;

#endif
