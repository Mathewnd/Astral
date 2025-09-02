#ifndef _SPINLOCK_H
#define _SPINLOCK_H

#include <stdbool.h>
#include <kernel/interrupt.h>

typedef int spinlock_t;

#define SPINLOCK_INIT_VALUE 0
#define SPINLOCK_INIT(x) x = SPINLOCK_INIT_VALUE
#define SPINLOCK_DEFINE(x) spinlock_t x = SPINLOCK_INIT_VALUE

static inline bool spinlock_try(spinlock_t *lock) {
	return __sync_bool_compare_and_swap(lock, 0, 1);
}

static inline void spinlock_acquire(spinlock_t *lock) {
	while (!__sync_bool_compare_and_swap(lock, 0, 1)) {
		while (*lock) asm volatile ("pause" : : : "memory");
	}
}

static inline bool spinlock_acquire_irq_clear(spinlock_t *lock) {
	bool ret = interrupt_set(false);
	spinlock_acquire(lock);
	return ret;
}

static inline void spinlock_release(spinlock_t *lock) {
	*lock = 0;
}

static inline void spinlock_release_irq_restore(spinlock_t *lock, bool irqstate) {
	spinlock_release(lock);
	interrupt_set(irqstate);
}

static inline long spinlock_acquire_raise_ipl(spinlock_t *lock, long ipl) {
	long ret = interrupt_raiseipl(ipl);
	spinlock_acquire(lock);
	return ret;
}

static inline void spinlock_release_lower_ipl(spinlock_t *lock, long ipl) {
	spinlock_release(lock);
	interrupt_loweripl(ipl);
}

#endif
