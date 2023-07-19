#ifndef _UTIL_H
#define _UTIL_H

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

#endif
