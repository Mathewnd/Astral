#ifndef _UTIL_H
#define _UTIL_H

#define ROUND_DOWN(v, n) ((v) - ((v) % (n)))
#define ROUND_UP(v, n) ROUND_DOWN((v) + (n) - 1, n)

static inline long abs(long x) {
	return x < 0 ? -x : x;
}

static inline long ilog2(long x) {
	return sizeof(long) * 8 - __builtin_clz(x) - 1;
}

#endif
