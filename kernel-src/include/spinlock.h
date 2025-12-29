#ifndef _SPINLOCK_H
#define _SPINLOCK_H

#include <stdbool.h>
#include <kernel/interrupt.h>

typedef uintptr_t spinlock_t;

#define SPINLOCK_INIT_VALUE 0
#define SPINLOCK_INIT(x) x = SPINLOCK_INIT_VALUE
#define SPINLOCK_DEFINE(x) spinlock_t x = SPINLOCK_INIT_VALUE

static inline bool spinlock_try(spinlock_t *lock) {
	spinlock_t expected = SPINLOCK_INIT_VALUE;
	return __atomic_compare_exchange_n(lock, &expected, (uintptr_t)__builtin_return_address(0), false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static inline void spinlock_acquire(spinlock_t *lock) {
	spinlock_t expected = SPINLOCK_INIT_VALUE;
	while (!__atomic_compare_exchange_n(lock, &expected, (uintptr_t)__builtin_return_address(0), false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
		while (*lock) asm volatile ("pause" : : : "memory");
		expected = SPINLOCK_INIT_VALUE;
	}
}

static inline bool spinlock_acquire_irq_clear(spinlock_t *lock) {
	bool ret = interrupt_set(false);
	spinlock_acquire(lock);
	return ret;
}

static inline void spinlock_release(spinlock_t *lock) {
	__atomic_store_n(lock, 0, __ATOMIC_RELEASE);
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
