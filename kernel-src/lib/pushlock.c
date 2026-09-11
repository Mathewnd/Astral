#include <pushlock.h>
#include <semaphore.h>
#include <logging.h>

#define CAS_ACQUIRE(ptr, saved, new) \
	__atomic_compare_exchange_n(ptr, &saved, new, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)

#define CAS_RELEASE(ptr, saved, new) \
	__atomic_compare_exchange_n(ptr, &saved, new, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)

#define PUSHLOCK_FLAGS_ACQUIRED 1
#define PUSHLOCK_FLAGS_EXCLUSIVE 2
#define PUSHLOCK_FLAGS_CONTENDED 4

#define PUSHLOCK_MASK_FLAGS \
	(PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE | PUSHLOCK_FLAGS_CONTENDED)

#define PUSHLOCK_VALUE_ALIGNMENT (PUSHLOCK_MASK_FLAGS + 1)
#define PUSHLOCK_MASK_POINTER_SHARED_COUNT (~(pushlock_t)PUSHLOCK_MASK_FLAGS)

_Static_assert(
	(PUSHLOCK_VALUE_ALIGNMENT & (PUSHLOCK_VALUE_ALIGNMENT - 1)) == 0,
	"push lock flags must occupy contiguous low bits");

typedef struct pushlock_wait_block_t {
	struct pushlock_wait_block_t *next;
	size_t shared_count;
	bool exclusive;
	semaphore_t semaphore;
} __attribute__((aligned(PUSHLOCK_VALUE_ALIGNMENT))) pushlock_wait_block_t;

#define PUSHLOCK_GET_POINTER(x) \
	((pushlock_wait_block_t *)((x) & PUSHLOCK_MASK_POINTER_SHARED_COUNT))

#define PUSHLOCK_GET_SHARED_COUNT(x) \
	(((pushlock_t)(x) & PUSHLOCK_MASK_POINTER_SHARED_COUNT) / PUSHLOCK_VALUE_ALIGNMENT)

#define PUSHLOCK_INCREMENT_SHARED_COUNT(x) \
	((x) + PUSHLOCK_VALUE_ALIGNMENT)

#define PUSHLOCK_DECREMENT_SHARED_COUNT(x) \
	((x) - PUSHLOCK_VALUE_ALIGNMENT)

bool pushlock_try_acquire_exclusive(pushlock_t *pushlock) {
retry_lock:
	pushlock_t saved_value = 0;
	if (CAS_ACQUIRE(pushlock, saved_value, PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
		return true;

	if (saved_value & PUSHLOCK_FLAGS_ACQUIRED)
		return false;

	if (!CAS_ACQUIRE(pushlock, saved_value, saved_value | PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
		goto retry_lock;

	return true;
}

void pushlock_acquire_exclusive(pushlock_t *pushlock) {
retry_lock:
	pushlock_t saved_value = 0;
	if (CAS_ACQUIRE(pushlock, saved_value, PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
		return;

	if ((saved_value & PUSHLOCK_FLAGS_ACQUIRED) == 0) {
		if (!CAS_ACQUIRE(pushlock, saved_value, saved_value | PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
			goto retry_lock;

		return;
	}

	pushlock_wait_block_t wait_block;
	SEMAPHORE_INIT(&wait_block.semaphore, 0);
	wait_block.exclusive = true;

	if (saved_value & PUSHLOCK_FLAGS_CONTENDED) {
		wait_block.next = PUSHLOCK_GET_POINTER(saved_value);
		__atomic_store_n(&wait_block.shared_count, 0, __ATOMIC_RELAXED);
	} else {
		wait_block.next = NULL;
		__atomic_store_n(&wait_block.shared_count, PUSHLOCK_GET_SHARED_COUNT(saved_value), __ATOMIC_RELAXED);
	}

	if (CAS_RELEASE(pushlock, saved_value, (uintptr_t)&wait_block | (saved_value & PUSHLOCK_MASK_FLAGS) | PUSHLOCK_FLAGS_CONTENDED))
		semaphore_wait(&wait_block.semaphore, false);

	goto retry_lock;
}

bool pushlock_try_acquire_shared(pushlock_t *pushlock) {
retry_lock:
	pushlock_t saved_value = 0;
	if (CAS_ACQUIRE(pushlock, saved_value, PUSHLOCK_INCREMENT_SHARED_COUNT(PUSHLOCK_FLAGS_ACQUIRED)))
		return true;

	if (saved_value & PUSHLOCK_FLAGS_EXCLUSIVE)
		return false;

	if ((saved_value & PUSHLOCK_FLAGS_CONTENDED) == 0) {
		if (!CAS_ACQUIRE(pushlock, saved_value, PUSHLOCK_INCREMENT_SHARED_COUNT(saved_value)))
			goto retry_lock;

		return true;
	}

	if (saved_value & PUSHLOCK_FLAGS_ACQUIRED)
		return false;

	// The waiter pointer occupies the shared count field, so acquire as a single owner while contended.
	if (!CAS_ACQUIRE(pushlock, saved_value, saved_value | PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
		goto retry_lock;

	return true;
}

void pushlock_acquire_shared(pushlock_t *pushlock) {
retry_lock:
	pushlock_t saved_value = 0;
	if (CAS_ACQUIRE(pushlock, saved_value, PUSHLOCK_INCREMENT_SHARED_COUNT(PUSHLOCK_FLAGS_ACQUIRED)))
		return;

	if ((saved_value & PUSHLOCK_FLAGS_EXCLUSIVE) || ((saved_value & PUSHLOCK_FLAGS_ACQUIRED) && (saved_value & PUSHLOCK_FLAGS_CONTENDED))) {
		pushlock_wait_block_t wait_block;
		SEMAPHORE_INIT(&wait_block.semaphore, 0);
		wait_block.exclusive = false;

		wait_block.next = PUSHLOCK_GET_POINTER(saved_value);
		__atomic_store_n(&wait_block.shared_count, 0, __ATOMIC_RELAXED);

		if (CAS_RELEASE(pushlock, saved_value, (uintptr_t)&wait_block | (saved_value & PUSHLOCK_MASK_FLAGS) | PUSHLOCK_FLAGS_CONTENDED))
			semaphore_wait(&wait_block.semaphore, false);

		goto retry_lock;
	}

	if (saved_value & PUSHLOCK_FLAGS_ACQUIRED) {
		if (!CAS_ACQUIRE(pushlock, saved_value, PUSHLOCK_INCREMENT_SHARED_COUNT(saved_value)))
			goto retry_lock;

		return;
	}

	if (!CAS_ACQUIRE(pushlock, saved_value, saved_value | PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
		goto retry_lock;
}

static void pushlock_signal_waiters(pushlock_wait_block_t *wait_block) {
	pushlock_wait_block_t *next_wait_block;
	pushlock_wait_block_t *previous_wait_block = NULL;

	// Reverse the detached chain to signal older waiters first.
	while (wait_block) {
		next_wait_block = wait_block->next;
		wait_block->next = previous_wait_block;
		previous_wait_block = wait_block;
		wait_block = next_wait_block;
	}

	wait_block = previous_wait_block;

	while (wait_block) {
		// The wait block is stack allocated, so save next before waking.
		next_wait_block = wait_block->next;
		semaphore_signal(&wait_block->semaphore);
		wait_block = next_wait_block;
	}
}

static void pushlock_wake_waiters(pushlock_t *pushlock) {
	pushlock_t saved_value;
	pushlock_t new_value;
	pushlock_wait_block_t *head_wait_block;
	pushlock_wait_block_t *penultimate_wait_block;
	pushlock_wait_block_t *oldest_wait_block;

	for (;;) {
		// Wait blocks are published with release semantics. Acquire
		// the current head before traversing the chain.
		saved_value = __atomic_load_n(pushlock, __ATOMIC_ACQUIRE);

#ifdef DEBUG_LOCKS
		__assert(saved_value & PUSHLOCK_FLAGS_ACQUIRED);
		__assert(saved_value & PUSHLOCK_FLAGS_CONTENDED);
#endif

		head_wait_block = PUSHLOCK_GET_POINTER(saved_value);
		penultimate_wait_block = NULL;
		oldest_wait_block = head_wait_block;

		while (oldest_wait_block->next) {
			penultimate_wait_block = oldest_wait_block;
			oldest_wait_block = oldest_wait_block->next;
		}

		// Wake an exclusive oldest waiter alone, leaving newer waiters queued.
		if (oldest_wait_block->exclusive && penultimate_wait_block != NULL) {
			// New waiters can only be inserted at the head, so they
			// can't modify this tail link.
			penultimate_wait_block->next = NULL;

			for (;;) {
				new_value =
					(saved_value & PUSHLOCK_MASK_POINTER_SHARED_COUNT) |
					PUSHLOCK_FLAGS_CONTENDED;

				if (CAS_RELEASE(pushlock, saved_value, new_value)) {
					break;
				}

				// A waiter was inserted at the head. The failed CAS
				// updated saved_value, while the detached tail stays
				// unchanged.
			}

			semaphore_signal(&oldest_wait_block->semaphore);
			return;
		}

		// A single waiter, or a chain whose oldest waiter is shared, can be
		// detached completely. Woken waiters retry acquisition without handoff.
		if (CAS_RELEASE(pushlock, saved_value, 0)) {
			pushlock_signal_waiters(head_wait_block);
			return;
		}

		// The chain changed before it could be detached. Restart with
		// an acquire load before traversing it again.
	}
}

void pushlock_release_exclusive(pushlock_t *pushlock) {
	pushlock_t saved_value;

	saved_value = PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE;
	if (CAS_RELEASE(pushlock, saved_value, 0))
		return;

#ifdef DEBUG_LOCKS
	__assert(saved_value & PUSHLOCK_FLAGS_ACQUIRED);
	__assert(saved_value & PUSHLOCK_FLAGS_EXCLUSIVE);
#endif

	pushlock_wake_waiters(pushlock);
}

static void pushlock_release_shared_contended(pushlock_t *pushlock, pushlock_t saved_value) {
	pushlock_wait_block_t *oldest_wait_block;

	oldest_wait_block = PUSHLOCK_GET_POINTER(saved_value);

	while (oldest_wait_block->next)
		oldest_wait_block = oldest_wait_block->next;

	if (__atomic_sub_fetch(&oldest_wait_block->shared_count, 1, __ATOMIC_RELEASE))
		return;

	__atomic_thread_fence(__ATOMIC_ACQUIRE);

	pushlock_wake_waiters(pushlock);
}

void pushlock_release_shared(pushlock_t *pushlock) {
retry_fast_path:
	pushlock_t saved_value = PUSHLOCK_INCREMENT_SHARED_COUNT(PUSHLOCK_FLAGS_ACQUIRED);
	if (CAS_RELEASE(pushlock, saved_value, 0))
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

	if (!CAS_RELEASE(pushlock, saved_value, PUSHLOCK_DECREMENT_SHARED_COUNT(saved_value)))
		goto retry_fast_path;
}
