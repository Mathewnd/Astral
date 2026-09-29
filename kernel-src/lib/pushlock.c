#include <pushlock.h>
#include <semaphore.h>
#include <logging.h>
#include <kernel/piab.h>
#include <errno.h>

// A push lock is exactly one pointer-sized word.
//
// Without PUSHLOCK_STATE_CONTENDED, the high bits contain the shared owner
// count:
//
//   0                                 unlocked
//   ACQUIRED                          exclusive owner
//   ACQUIRED | n*SHARED_INC           n shared owners
//
// With PUSHLOCK_STATE_CONTENDED, the high bits point at the newest stack
// allocated waiter. Waiters link toward older waiters. `newer` and `oldest` are
// populated lazily by the owner of PUSHLOCK_STATE_WAKING so releases don't
// repeatedly scan the whole chain.
//
// PUSHLOCK_STATE_WAKING allows one thread to update queue links and choose whom
// to wake. It doesn't grant lock ownership. Other threads may still add
// waiters, and an exclusive acquirer may set ACQUIRED while maintenance is in
// progress. Shared releasers may walk the chain while their displaced ownership
// keeps it alive.
//
// If contention begins while more than one shared owner exists, the exact
// displaced reader count is stored in the oldest waiter. A zero waiter count
// means there's no multi-reader count to maintain; for a shared owner in
// contended mode that means it's the sole shared owner.
//
// Every waiter, including its semaphore, is initialized before its address is
// stored in the lock word. The semaphore retains an early signal, so
// semaphore_signal() may safely finish before semaphore_wait() starts.

#define PUSHLOCK_STATE_ACQUIRED  ((pushlock_t)1u << 0)
#define PUSHLOCK_STATE_CONTENDED ((pushlock_t)1u << 1)
#define PUSHLOCK_STATE_WAKING    ((pushlock_t)1u << 2)
#define PUSHLOCK_SHARED_INC      ((pushlock_t)1u << 3)

#define PUSHLOCK_STATE_MASK \
	(PUSHLOCK_STATE_ACQUIRED | PUSHLOCK_STATE_CONTENDED | PUSHLOCK_STATE_WAKING)

#define PUSHLOCK_VALUE_ALIGNMENT (PUSHLOCK_STATE_MASK + 1)
#define PUSHLOCK_MASK_POINTER_SHARED_COUNT (~PUSHLOCK_STATE_MASK)

#define PUSHLOCK_GET_POINTER(state) \
	((pushlock_wait_block_t *)((state) & PUSHLOCK_MASK_POINTER_SHARED_COUNT))

#define PUSHLOCK_GET_SHARED_COUNT(state) \
	((size_t)((state) / PUSHLOCK_SHARED_INC))

typedef struct pushlock_wait_block_t {
	struct pushlock_wait_block_t *older;
	struct pushlock_wait_block_t *newer;
	struct pushlock_wait_block_t *oldest;
	size_t shared_count;
	semaphore_t semaphore;
	bool exclusive;
	piab_record_t *piab_record;
} __attribute__((aligned(PUSHLOCK_VALUE_ALIGNMENT))) pushlock_wait_block_t;

_Static_assert(sizeof(pushlock_t) == sizeof(uintptr_t),
	"push locks must fit in one uintptr_t");
_Static_assert(PUSHLOCK_VALUE_ALIGNMENT == 8,
	"push lock flags must occupy exactly the low three bits");
_Static_assert(PUSHLOCK_SHARED_INC == PUSHLOCK_VALUE_ALIGNMENT,
	"shared count must start above the pointer tag bits");
_Static_assert(_Alignof(pushlock_wait_block_t) >= PUSHLOCK_VALUE_ALIGNMENT,
	"pushlock waiters must preserve the low three pointer bits");

static inline pushlock_t pushlock_load(const pushlock_t *pushlock) {
	return __atomic_load_n(pushlock, __ATOMIC_ACQUIRE);
}

// ACQUIRING and RETRYING already use the owner tree if a switch occurs between the cmpxchg and post_acquire
static inline bool pushlock_cas_acquire(pushlock_t *pushlock, pushlock_t *expected, pushlock_t desired, piab_record_t *record) {
	bool acquired;

	acquired = __atomic_compare_exchange_n(pushlock, expected, desired, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
	if (acquired && record)
		piab_post_acquire(record);
	return acquired;
}

static inline bool pushlock_cas_acq_rel(pushlock_t *pushlock, pushlock_t *expected, pushlock_t desired) {
	return __atomic_compare_exchange_n(pushlock, expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static inline void pushlock_assert_state(pushlock_t state) {
#ifdef DEBUG_LOCKS
	if ((state & PUSHLOCK_STATE_CONTENDED) == 0) {
		__assert((state & PUSHLOCK_STATE_WAKING) == 0);
		if ((state & PUSHLOCK_STATE_ACQUIRED) == 0)
			__assert(state == 0);
	} else {
		__assert(PUSHLOCK_GET_POINTER(state) != NULL);
	}
#else
	(void)state;
#endif
}

// Shared ownership keeps the chain alive. Only read links here as another CPU
// may hold WAKING and be completing the reverse links.
static pushlock_wait_block_t *pushlock_find_oldest(pushlock_wait_block_t *waiter) {
	for (;;) {
		pushlock_wait_block_t *cached = __atomic_load_n(&waiter->oldest, __ATOMIC_ACQUIRE);
		if (cached != NULL)
			return cached;

		waiter = waiter->older;
#ifdef DEBUG_LOCKS
		__assert(waiter != NULL);
#endif
	}
}

// Called with WAKING held. Stop at the portion already linked by an earlier
// pass, then cache its oldest entry at the current head.
static pushlock_wait_block_t *pushlock_complete_waiter_links(pushlock_wait_block_t *head) {
	pushlock_wait_block_t *waiter = head;

	for (;;) {
		pushlock_wait_block_t *cached = __atomic_load_n(&waiter->oldest, __ATOMIC_ACQUIRE);
		if (cached != NULL) {
			if (waiter != head)
				__atomic_store_n(&head->oldest, cached, __ATOMIC_RELEASE);
			return cached;
		}

		pushlock_wait_block_t *older = waiter->older;
#ifdef DEBUG_LOCKS
		__assert(older != NULL);
#endif

		older->newer = waiter;
		waiter = older;
	}
}

// Called by the thread that acquired WAKING. Clear it while ACQUIRED remains
// set or wake waiters after observing ACQUIRED clear.
static void pushlock_update_waiters(pushlock_t *pushlock, pushlock_t state) {
	pushlock_wait_block_t *head;
	pushlock_wait_block_t *oldest;
	pushlock_wait_block_t *new_oldest;
	pushlock_t desired;
	pushlock_t previous;

	for (;;) {
		pushlock_assert_state(state);
#ifdef DEBUG_LOCKS
		__assert(state & PUSHLOCK_STATE_CONTENDED);
		__assert(state & PUSHLOCK_STATE_WAKING);
#endif

		head = PUSHLOCK_GET_POINTER(state);
		oldest = pushlock_complete_waiter_links(head);

		if (state & PUSHLOCK_STATE_ACQUIRED) {
			// Recheck both the head and ACQUIRED. A release may leave wakeup to us
			// while a newly inserted head still needs its links completed.
			desired = state & ~PUSHLOCK_STATE_WAKING;
			if (pushlock_cas_acq_rel(pushlock, &state, desired))
				return;
			continue;
		}

		new_oldest = oldest->newer;

#ifdef DEBUG_LOCKS
		__assert(oldest->older == NULL);
		__assert(__atomic_load_n(&oldest->shared_count, __ATOMIC_ACQUIRE) == 0);
		__assert((oldest == head) == (new_oldest == NULL));
#endif

		if (oldest->exclusive && new_oldest != NULL) {
#ifdef DEBUG_LOCKS
			__assert(new_oldest->older == oldest);
#endif
			// No shared releaser remains after ACQUIRED clears. Cut the old
			// tail and replace before the waiter can leave its stack.
			new_oldest->older = NULL;
			__atomic_store_n(&new_oldest->oldest, new_oldest, __ATOMIC_RELEASE);

			if (head != new_oldest)
				__atomic_store_n(&head->oldest, new_oldest, __ATOMIC_RELEASE);

			oldest->newer = NULL;

			// Preserve a concurrently inserted head or exclusive acquisition
			previous = __atomic_fetch_and(pushlock, ~PUSHLOCK_STATE_WAKING, __ATOMIC_RELEASE);
			(void)previous;
#ifdef DEBUG_LOCKS
			__assert(previous & PUSHLOCK_STATE_WAKING);
			__assert(previous & PUSHLOCK_STATE_CONTENDED);
#endif
			piab_pre_wakeup(oldest->piab_record);
			semaphore_signal(&oldest->semaphore);
			return;
		}

		// Remove the queue only if its head and the unlocked state still match
		if (!pushlock_cas_acq_rel(pushlock, &state, 0))
			continue;

		// Read newer before signalling since the waiter may return immediately
		while (oldest != NULL) {
			new_oldest = oldest->newer;
			piab_pre_wakeup(oldest->piab_record);
			semaphore_signal(&oldest->semaphore);
			oldest = new_oldest;
		}

		return;
	}
}

// With CONTENDED set, the high bits hold a waiter pointer instead of a
// shared count. New readers queue rather than becoming an uncounted owner.
static inline bool pushlock_shared_acquire_value(pushlock_t state, pushlock_t *desired) {
	if (state & PUSHLOCK_STATE_CONTENDED)
		return false;

	if ((state & PUSHLOCK_STATE_ACQUIRED) == 0) {
		*desired = state | PUSHLOCK_STATE_ACQUIRED | PUSHLOCK_SHARED_INC;
		return true;
	}

	if (PUSHLOCK_GET_SHARED_COUNT(state) != 0) {
#ifdef DEBUG_LOCKS
		__assert(state <= UINTPTR_MAX - PUSHLOCK_SHARED_INC);
#endif
		*desired = state + PUSHLOCK_SHARED_INC;
		return true;
	}

	return false;
}

static pushlock_t pushlock_queue_and_wait(pushlock_t *pushlock, pushlock_t state, bool exclusive, piab_record_t *record) {
	pushlock_wait_block_t waiter;
	pushlock_t desired;
	size_t readers;
	bool acquired_waking;
	int result;

	waiter.newer = NULL;
	waiter.exclusive = exclusive;
	waiter.piab_record = record;
	// A releaser may signal as soon as the publication CMPXCHG succeeds
	SEMAPHORE_INIT(&waiter.semaphore, 0);

#ifdef DEBUG_LOCKS
	__assert(((uintptr_t)&waiter & PUSHLOCK_STATE_MASK) == 0);
#endif

	for (;;) {
		acquired_waking = false;

		pushlock_assert_state(state);
		if (exclusive) {
			if (!(state & PUSHLOCK_STATE_ACQUIRED))
				return state;
		} else if (pushlock_shared_acquire_value(state, &desired)) {
			return state;
		}

		// A failed CMPXCHG may return a different queue head, so rebuild from state
		waiter.older = NULL;
		__atomic_store_n(&waiter.oldest, NULL, __ATOMIC_RELAXED);
		__atomic_store_n(&waiter.shared_count, 0, __ATOMIC_RELAXED);

		if (state & PUSHLOCK_STATE_CONTENDED) {
			// Link this waiter to the current queue head before publishing it
			waiter.older = PUSHLOCK_GET_POINTER(state);
			desired = (pushlock_t)(uintptr_t)&waiter | (state & PUSHLOCK_STATE_MASK) | PUSHLOCK_STATE_CONTENDED | PUSHLOCK_STATE_WAKING;
			acquired_waking = !(state & PUSHLOCK_STATE_WAKING);
		} else {
			readers = PUSHLOCK_GET_SHARED_COUNT(state);
#ifdef DEBUG_LOCKS
			if (!exclusive)
				__assert(readers == 0);
#endif

			__atomic_store_n(&waiter.oldest, &waiter, __ATOMIC_RELAXED);
			desired = (pushlock_t)(uintptr_t)&waiter | PUSHLOCK_STATE_ACQUIRED | PUSHLOCK_STATE_CONTENDED;

			// Multiple existing readers need their count moved into the wait block
			if (readers > 1)
				__atomic_store_n(&waiter.shared_count, readers, __ATOMIC_RELAXED);
		}

		// A releaser can wake this waiter as soon as the cmpxchg succeeds
		piab_pre_wait(record);
		if (!pushlock_cas_acq_rel(pushlock, &state, desired)) {
			piab_prepare_retry(record);
			continue;
		}

		if (acquired_waking)
			pushlock_update_waiters(pushlock, desired);

		result = semaphore_wait(&waiter.semaphore, false);
#ifdef DEBUG_LOCKS
		__assert(result == 0);
#endif
		(void)result;
		return pushlock_load(pushlock);
	}
}

static inline __attribute__((always_inline)) bool pushlock_try_exclusive(pushlock_t *pushlock, piab_record_t *record) {
	pushlock_t state = 0;

	if (pushlock_cas_acquire(pushlock, &state, PUSHLOCK_STATE_ACQUIRED, record))
		return true;

	for (;;) {
		pushlock_assert_state(state);

		if (state & PUSHLOCK_STATE_ACQUIRED)
			return false;

		pushlock_t desired = state | PUSHLOCK_STATE_ACQUIRED;
		if (pushlock_cas_acquire(pushlock, &state, desired, record))
			return true;
	}
}

static inline __attribute__((always_inline)) void pushlock_exclusive(pushlock_t *pushlock, piab_record_t *record) {
	pushlock_t state = 0;

	if (pushlock_cas_acquire(pushlock, &state, PUSHLOCK_STATE_ACQUIRED, record))
		return;

	for (;;) {
		pushlock_assert_state(state);

		if ((state & PUSHLOCK_STATE_ACQUIRED) == 0) {
			pushlock_t desired = state | PUSHLOCK_STATE_ACQUIRED;
			if (pushlock_cas_acquire(pushlock, &state, desired, record))
				return;

			continue;
		}

		state = pushlock_queue_and_wait(pushlock, state, true, record);
	}
}

static inline __attribute__((always_inline)) bool pushlock_try_shared(pushlock_t *pushlock, piab_record_t *record) {
	pushlock_t state = 0;

	if (pushlock_cas_acquire(pushlock, &state, PUSHLOCK_STATE_ACQUIRED | PUSHLOCK_SHARED_INC, record))
		return true;

	for (;;) {
		pushlock_assert_state(state);

		pushlock_t desired;
		if (!pushlock_shared_acquire_value(state, &desired))
			return false;

		if (pushlock_cas_acquire(pushlock, &state, desired, record))
			return true;
	}
}

static inline __attribute__((always_inline)) void pushlock_shared(pushlock_t *pushlock, piab_record_t *record) {
	pushlock_t state = 0;

	if (pushlock_cas_acquire(pushlock, &state, PUSHLOCK_STATE_ACQUIRED | PUSHLOCK_SHARED_INC, record))
		return;

	for (;;) {
		pushlock_assert_state(state);

		pushlock_t desired;
		if (pushlock_shared_acquire_value(state, &desired)) {
			if (pushlock_cas_acquire(pushlock, &state, desired, record))
				return;

			continue;
		}

		state = pushlock_queue_and_wait(pushlock, state, false, record);
	}
}

// Clear ACQUIRED and set WAKING in one CMPXCHG. If WAKING was already set,
// the thread processing the queue will handle the release.
static void pushlock_release_contended(pushlock_t *pushlock, pushlock_t state) {
	pushlock_t desired;
	bool acquired_waking;

	for (;;) {
		pushlock_assert_state(state);
#ifdef DEBUG_LOCKS
		__assert(state & PUSHLOCK_STATE_ACQUIRED);
		__assert(state & PUSHLOCK_STATE_CONTENDED);
#endif
		acquired_waking = !(state & PUSHLOCK_STATE_WAKING);
		desired = state & ~PUSHLOCK_STATE_ACQUIRED;
		if (acquired_waking)
			desired |= PUSHLOCK_STATE_WAKING;

		if (!pushlock_cas_acq_rel(pushlock, &state, desired))
			continue;

		if (acquired_waking)
			pushlock_update_waiters(pushlock, desired);
		return;
	}
}

static void pushlock_release_exclusive_internal(pushlock_t *pushlock) {
	pushlock_t state = PUSHLOCK_STATE_ACQUIRED;

	if (pushlock_cas_acq_rel(pushlock, &state, 0))
		return;

	pushlock_release_contended(pushlock, state);
}

static void pushlock_release_shared_internal(pushlock_t *pushlock) {
	pushlock_t state = PUSHLOCK_STATE_ACQUIRED | PUSHLOCK_SHARED_INC;

	if (pushlock_cas_acq_rel(pushlock, &state, 0))
		return;

	for (;;) {
		pushlock_assert_state(state);
#ifdef DEBUG_LOCKS
		__assert(state & PUSHLOCK_STATE_ACQUIRED);
#endif

		if ((state & PUSHLOCK_STATE_CONTENDED) == 0) {
			size_t readers = PUSHLOCK_GET_SHARED_COUNT(state);
#ifdef DEBUG_LOCKS
			__assert(readers != 0);
#endif
			pushlock_t desired = readers > 1 ? state - PUSHLOCK_SHARED_INC : 0;
			if (pushlock_cas_acq_rel(pushlock, &state, desired))
				return;
			continue;
		}

		// The oldest wait block stays valid until this reader drops its count
		pushlock_wait_block_t *oldest = pushlock_find_oldest(PUSHLOCK_GET_POINTER(state));
		size_t displaced = __atomic_load_n(&oldest->shared_count, __ATOMIC_ACQUIRE);

		if (displaced != 0) {
			size_t previous = __atomic_fetch_sub(&oldest->shared_count, 1, __ATOMIC_ACQ_REL);

#ifdef DEBUG_LOCKS
			__assert(previous != 0);
#endif

			if (previous != 1)
				return;

			state = pushlock_load(pushlock);
		}

		pushlock_release_contended(pushlock, state);
		return;
	}
}

bool pushlock_try_acquire_exclusive(pushlock_t *pushlock) {
	return pushlock_try_acquire_flags(pushlock, PUSHLOCK_FLAGS_EXCLUSIVE) == 0;
}

bool pushlock_try_acquire_shared(pushlock_t *pushlock) {
	return pushlock_try_acquire_flags(pushlock, 0) == 0;
}

void pushlock_acquire_exclusive(pushlock_t *pushlock) {
	(void)pushlock_acquire_flags(pushlock, PUSHLOCK_FLAGS_EXCLUSIVE);
}

void pushlock_acquire_shared(pushlock_t *pushlock) {
	(void)pushlock_acquire_flags(pushlock, 0);
}

int pushlock_acquire_fast(pushlock_t *pushlock, unsigned flags, piab_record_t **result) {
	piab_record_t *record = NULL;

#ifdef DEBUG_LOCKS
	__assert(result);
#endif
	*result = NULL;
	if (flags & ~PUSHLOCK_VALID_FLAGS)
		return EINVAL;

	if (!(flags & PUSHLOCK_FLAGS_DISABLE_PIAB_TRACKING))
		record = piab_pre_acquire(pushlock, flags & PUSHLOCK_FLAGS_EXCLUSIVE);

	if (flags & PUSHLOCK_FLAGS_EXCLUSIVE)
		pushlock_exclusive(pushlock, record);
	else
		pushlock_shared(pushlock, record);

	// Exhaustion permits an acquisition without a record.
	// Explicitly disabled tracking must not set piab_tracking_exhausted.
	if (!(flags & PUSHLOCK_FLAGS_DISABLE_PIAB_TRACKING) && record == NULL)
		piab_post_acquire(NULL);
	*result = record;
	return 0;
}

int pushlock_try_acquire_fast(pushlock_t *pushlock, unsigned flags, piab_record_t **result) {
	piab_record_t *record = NULL;
	bool acquired;

#ifdef DEBUG_LOCKS
	__assert(result);
#endif
	*result = NULL;
	if (flags & ~PUSHLOCK_VALID_FLAGS)
		return EINVAL;

	if (!(flags & PUSHLOCK_FLAGS_DISABLE_PIAB_TRACKING))
		record = piab_pre_acquire(pushlock, flags & PUSHLOCK_FLAGS_EXCLUSIVE);

	if (flags & PUSHLOCK_FLAGS_EXCLUSIVE)
		acquired = pushlock_try_exclusive(pushlock, record);
	else
		acquired = pushlock_try_shared(pushlock, record);

	if (!acquired) {
		piab_cancel_acquire(record);
		return EBUSY;
	}

	if (!(flags & PUSHLOCK_FLAGS_DISABLE_PIAB_TRACKING) && record == NULL)
		piab_post_acquire(NULL);
	*result = record;
	return 0;
}

int pushlock_acquire_flags(pushlock_t *pushlock, unsigned flags) {
	piab_record_t *record;
	return pushlock_acquire_fast(pushlock, flags, &record);
}

int pushlock_try_acquire_flags(pushlock_t *pushlock, unsigned flags) {
	piab_record_t *record;
	return pushlock_try_acquire_fast(pushlock, flags, &record);
}

void pushlock_release_flags(pushlock_t *pushlock, unsigned flags) {
#ifdef DEBUG_LOCKS
	__assert((flags & ~PUSHLOCK_VALID_FLAGS) == 0);
#endif
	if (flags & PUSHLOCK_FLAGS_EXCLUSIVE)
		pushlock_release_exclusive_internal(pushlock);
	else
		pushlock_release_shared_internal(pushlock);

	if (!(flags & PUSHLOCK_FLAGS_DISABLE_PIAB_TRACKING))
		piab_post_release(pushlock, flags & PUSHLOCK_FLAGS_EXCLUSIVE);
}

void pushlock_release_fast(pushlock_t *pushlock, unsigned flags, piab_record_t *record) {
#ifdef DEBUG_LOCKS
	__assert((flags & ~PUSHLOCK_VALID_FLAGS) == 0);
	__assert(!(flags & PUSHLOCK_FLAGS_DISABLE_PIAB_TRACKING) || record == NULL);
#endif
	if (flags & PUSHLOCK_FLAGS_EXCLUSIVE)
		pushlock_release_exclusive_internal(pushlock);
	else
		pushlock_release_shared_internal(pushlock);

	if (!(flags & PUSHLOCK_FLAGS_DISABLE_PIAB_TRACKING))
		piab_post_release_fast(pushlock, flags & PUSHLOCK_FLAGS_EXCLUSIVE, record);
}

void pushlock_release_exclusive(pushlock_t *pushlock) {
	pushlock_release_flags(pushlock, PUSHLOCK_FLAGS_EXCLUSIVE);
}

void pushlock_release_shared(pushlock_t *pushlock) {
	pushlock_release_flags(pushlock, 0);
}
