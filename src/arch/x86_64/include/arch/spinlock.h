#ifndef _SPINLOCK_H_INCLUDE
#define _SPINLOCK_H_INCLUDE

#include <stdbool.h>

static inline void spinlock_acquire(int *lock){
	while(!__sync_bool_compare_and_swap(lock, 0, 1)) asm("pause");
}

static inline bool spinlock_trytoacquire(int *lock){
	return __sync_bool_compare_and_swap(lock, 0, 1);
}

static inline void spinlock_release(int *lock){
	*lock = 0;
}

#endif
