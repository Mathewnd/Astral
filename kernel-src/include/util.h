#ifndef _UTIL_H
#define _UTIL_H

#include <stdint.h>
#include <stddef.h>

#define UTIL_ZEROBUFFERSIZE (1024 * 64)

#define ROUND_DOWN(v, n) ((v) - ((v) % (n)))
#define ROUND_UP(v, n) ROUND_DOWN((v) + (n) - 1, n)

static inline long abs(long x) {
	return x < 0 ? -x : x;
}

static inline unsigned long log2(unsigned long x) {
	return sizeof(unsigned long) * 8 - __builtin_clzll(x) - 1;
}

static inline long min(long x, long y) {
	return x > y ? y : x;
}

#define FNV1PRIME  0x100000001b3ull
#define FNV1OFFSET 0xcbf29ce484222325ull

// FNV-1a hash implementation
static inline uint64_t fnv1ahash(void *buffer, size_t size) {
	uint8_t *ptr = buffer;
	uint8_t *top = ptr + size;
	uint64_t h = FNV1OFFSET;

	while (ptr < top) {
		h ^= *ptr++;
		h *= FNV1PRIME;
	}

	return h;
}

extern uint8_t util_zerobuffer[];

#endif
