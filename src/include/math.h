#ifndef _MATH_H_INCLUDE
#define _MATH_H_INCLUDE


long intpow(long base, long exp);

static inline long labs(long n){
	return n < 0 ? -n : n;
}

#endif
