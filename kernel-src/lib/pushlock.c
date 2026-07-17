#include <pushlock.h>
#include <semaphore.h>
#include <logging.h>

#define CAS_ACQUIRE__RELAXED(ptr, saved, new) \
	__atomic_compare_exchange_n(ptr, &saved, new, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)

#define CAS_RELEASE__RELAXED(ptr, saved, new) \
	__atomic_compare_exchange_n(ptr, &saved, new, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)

typedef struct pushlock_wait_block_t {
	struct pushlock_wait_block_t *next;
	size_t shared_count;
	semaphore_t semaphore;
} __attribute__((aligned(8))) pushlock_wait_block_t;

#define PUSHLOCK_FLAGS_ACQUIRED 1
#define PUSHLOCK_FLAGS_EXCLUSIVE 2
#define PUSHLOCK_FLAGS_CONTENDED 4
#define PUSHLOCK_MASK_FLAGS 0x7
#define PUSHLOCK_MASK_POINTER_SHARED_COUNT (0xfffffffffffffff8lu)

#define PUSHLOCK_GET_POINTER(x) ((pushlock_wait_block_t *)((x) & PUSHLOCK_MASK_POINTER_SHARED_COUNT))
#define PUSHLOCK_GET_SHARED_COUNT(x) ((uintptr_t)PUSHLOCK_GET_POINTER(x) >> 3)
#define PUSHLOCK_INCREMENT_SHARED_COUNT(x) ((x) + 0x8)
#define PUSHLOCK_DECREMENT_SHARED_COUNT(x) ((x) - 0x8)

bool pushlock_try_acquire_exclusive(pushlock_t *pushlock) {
retry_lock:
	pushlock_t saved_value = 0;
	if (CAS_ACQUIRE__RELAXED(pushlock, saved_value, PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
		return true;

	if (saved_value & PUSHLOCK_FLAGS_ACQUIRED)
		return false;

	if (!CAS_ACQUIRE__RELAXED(pushlock, saved_value, saved_value | PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
		goto retry_lock;

	return true;
}

void pushlock_acquire_exclusive(pushlock_t *pushlock) {
retry_lock:
	pushlock_t saved_value = 0;
	if (CAS_ACQUIRE__RELAXED(pushlock, saved_value, PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
		return;

	if ((saved_value & PUSHLOCK_FLAGS_ACQUIRED) == 0 && CAS_ACQUIRE__RELAXED(pushlock, saved_value, saved_value | PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
		return;

	pushlock_wait_block_t wait_block;
	SEMAPHORE_INIT(&wait_block.semaphore, 0);

	if (saved_value & PUSHLOCK_FLAGS_CONTENDED) {
		wait_block.next = PUSHLOCK_GET_POINTER(saved_value);
		__atomic_store_n(&wait_block.shared_count, 0, __ATOMIC_RELAXED);
	} else {
		wait_block.next = NULL;
		__atomic_store_n(&wait_block.shared_count, PUSHLOCK_GET_SHARED_COUNT(saved_value), __ATOMIC_RELAXED);
	}

	if (CAS_RELEASE__RELAXED(pushlock, saved_value, (uintptr_t)&wait_block | (saved_value & PUSHLOCK_MASK_FLAGS) | PUSHLOCK_FLAGS_CONTENDED))
		semaphore_wait(&wait_block.semaphore, false);

	goto retry_lock;
}

bool pushlock_try_acquire_shared(pushlock_t *pushlock) {
retry_lock:
	pushlock_t saved_value = 0;
	if (CAS_ACQUIRE__RELAXED(pushlock, saved_value, PUSHLOCK_INCREMENT_SHARED_COUNT(PUSHLOCK_FLAGS_ACQUIRED)))
		return true;

	if (saved_value & PUSHLOCK_FLAGS_EXCLUSIVE)
		return false;

	if ((saved_value & PUSHLOCK_FLAGS_CONTENDED) == 0) {
		if (!CAS_ACQUIRE__RELAXED(pushlock, saved_value, PUSHLOCK_INCREMENT_SHARED_COUNT(saved_value)))
			goto retry_lock;

		return true;
	}

	if (saved_value & PUSHLOCK_FLAGS_ACQUIRED)
		return false;

	// try to acquire as exclusive
	if (!CAS_ACQUIRE__RELAXED(pushlock, saved_value, saved_value | PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
		goto retry_lock;

	return true;
}

void pushlock_acquire_shared(pushlock_t *pushlock) {
retry_lock:
	pushlock_t saved_value = 0;
	if (CAS_ACQUIRE__RELAXED(pushlock, saved_value, PUSHLOCK_INCREMENT_SHARED_COUNT(PUSHLOCK_FLAGS_ACQUIRED)))
		return;

	if ((saved_value & PUSHLOCK_FLAGS_EXCLUSIVE) || ((saved_value & PUSHLOCK_FLAGS_ACQUIRED) && (saved_value & PUSHLOCK_FLAGS_CONTENDED))) {
		pushlock_wait_block_t wait_block;
		SEMAPHORE_INIT(&wait_block.semaphore, 0);

		wait_block.next = PUSHLOCK_GET_POINTER(saved_value);
		__atomic_store_n(&wait_block.shared_count, 0, __ATOMIC_RELAXED);

		if (CAS_RELEASE__RELAXED(pushlock, saved_value, (uintptr_t)&wait_block | (saved_value & PUSHLOCK_MASK_FLAGS) | PUSHLOCK_FLAGS_CONTENDED))
			semaphore_wait(&wait_block.semaphore, false);

		goto retry_lock;
	}

	if (saved_value & PUSHLOCK_FLAGS_ACQUIRED) {
		if (!CAS_ACQUIRE__RELAXED(pushlock, saved_value, PUSHLOCK_INCREMENT_SHARED_COUNT(saved_value)))
			goto retry_lock;

		return;
	}

	if (!CAS_ACQUIRE__RELAXED(pushlock, saved_value, saved_value | PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
		goto retry_lock;
}

// TODO: if we are releasing to a shared waiter, we should wake up every shared waiter
void pushlock_release_exclusive(pushlock_t *pushlock) {
	pushlock_t saved_value = PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE;
	if (CAS_RELEASE__RELAXED(pushlock, saved_value, 0))
		return;

#ifdef DEBUG_LOCKS
	__assert(saved_value & PUSHLOCK_FLAGS_ACQUIRED);
	__assert(saved_value & PUSHLOCK_FLAGS_EXCLUSIVE);
#endif

	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	pushlock_wait_block_t *wait_block = PUSHLOCK_GET_POINTER(saved_value);
	if (wait_block->next == NULL) {
		if (CAS_RELEASE__RELAXED(pushlock, saved_value, 0)) {
			semaphore_signal(&wait_block->semaphore);
			return;
		}

		// there are multiple waiters now, so we have to iterate through the list
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		wait_block = PUSHLOCK_GET_POINTER(saved_value);
	}

	while (wait_block->next->next)
		wait_block = wait_block->next;

	pushlock_wait_block_t *to_wake = wait_block->next;
	wait_block->next = NULL;

	while (!CAS_RELEASE__RELAXED(pushlock, saved_value, (saved_value & PUSHLOCK_MASK_POINTER_SHARED_COUNT) | PUSHLOCK_FLAGS_CONTENDED));

	semaphore_signal(&to_wake->semaphore);
}

static void pushlock_release_shared_contended(pushlock_t *pushlock, pushlock_t saved_value) {
	pushlock_wait_block_t *last_wait_block = PUSHLOCK_GET_POINTER(saved_value);
	pushlock_wait_block_t *penultimate_wait_block = NULL;
	if (last_wait_block->next) {
		penultimate_wait_block = last_wait_block;
		while (penultimate_wait_block->next->next)
			penultimate_wait_block = penultimate_wait_block->next;

		last_wait_block = penultimate_wait_block->next;
	}

	if (__atomic_sub_fetch(&last_wait_block->shared_count, 1, __ATOMIC_RELEASE))
		return;
	__atomic_thread_fence(__ATOMIC_ACQUIRE);

	if (penultimate_wait_block == NULL) {
		if (CAS_RELEASE__RELAXED(pushlock, saved_value, 0)) {
			semaphore_signal(&last_wait_block->semaphore);
			return;
		}

		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		penultimate_wait_block = PUSHLOCK_GET_POINTER(saved_value);
		while (penultimate_wait_block->next->next)
			penultimate_wait_block = penultimate_wait_block->next;
		last_wait_block = penultimate_wait_block->next;
	}

	penultimate_wait_block->next = NULL;

	while (!CAS_RELEASE__RELAXED(pushlock, saved_value, (saved_value & PUSHLOCK_MASK_POINTER_SHARED_COUNT) | PUSHLOCK_FLAGS_CONTENDED));

	semaphore_signal(&last_wait_block->semaphore);
}

void pushlock_release_shared(pushlock_t *pushlock) {
retry_fast_path:
	pushlock_t saved_value = PUSHLOCK_INCREMENT_SHARED_COUNT(PUSHLOCK_FLAGS_ACQUIRED);
	if (CAS_RELEASE__RELAXED(pushlock, saved_value, 0))
		return;

#ifdef DEBUG_LOCKS
	__assert(saved_value & PUSHLOCK_FLAGS_ACQUIRED);
#endif

	if (saved_value & PUSHLOCK_FLAGS_EXCLUSIVE) {
		pushlock_release_exclusive(pushlock);
		return;
	}

	if ((saved_value & PUSHLOCK_FLAGS_CONTENDED)) {
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		pushlock_release_shared_contended(pushlock, saved_value);
		return;
	}

	if (!CAS_RELEASE__RELAXED(pushlock, saved_value, PUSHLOCK_DECREMENT_SHARED_COUNT(saved_value)))
		goto retry_fast_path;
}
