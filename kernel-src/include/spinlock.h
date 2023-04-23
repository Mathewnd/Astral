#ifndef _SPINLOCK_H
#define _SPINLOCK_H

#include <stdbool.h>

typedef int spinlock_t;

#define SPINLOCK_INIT(x) x = 0;

static inline void spinlock_acquire(spinlock_t *lock){
	while(!__sync_bool_compare_and_swap(lock, 0, 1)) asm("pause");
}

static inline bool spinlock_try(spinlock_t *lock){
	return __sync_bool_compare_and_swap(lock, 0, 1);
}

static inline void spinlock_release(spinlock_t *lock){
	*lock = 0;
}

#endif
