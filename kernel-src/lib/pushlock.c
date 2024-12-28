#include <pushlock.h>

#define DO_CAS(ptr, saved, new) \
	__atomic_compare_exchange_n(ptr, &saved, new, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)

bool pushlock_try_acquire_exclusive(pushlock_t *pushlock) {
	pushlock_t saved_value = *pushlock;
	for (;;) {
		if ((PUSHLOCK_FLAGS_ACQUIRED & saved_value) == 0) {
			if (DO_CAS(pushlock, saved_value, saved_value | PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
				return true;

			continue;
		}

		return false;
	}
}

void pushlock_acquire_exclusive(pushlock_t *pushlock) {
	pushlock_t saved_value = *pushlock;
	for (;;) {
		if ((PUSHLOCK_FLAGS_ACQUIRED & saved_value) == 0) {
			if (DO_CAS(pushlock, saved_value, saved_value | PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
				return;

			continue;
		}


		pushlock_wait_block_t wait_block;
		SEMAPHORE_INIT(&wait_block.semaphore, 0);

		if (saved_value & PUSHLOCK_FLAGS_CONTENDED) {
			wait_block.next = PUSHLOCK_GET_POINTER(saved_value);
			wait_block.shared_count = 0;
		} else {
			wait_block.next = NULL;
			wait_block.shared_count = PUSHLOCK_GET_SHARED_COUNT(saved_value);
		}

		if (DO_CAS(pushlock, saved_value, (uintptr_t)&wait_block | (saved_value & PUSHLOCK_MASK_FLAGS) | PUSHLOCK_FLAGS_CONTENDED))
			semaphore_wait(&wait_block.semaphore, false);
	}
}

bool pushlock_try_acquire_shared(pushlock_t *pushlock) {
	pushlock_t saved_value = *pushlock;
	for (;;) {
		if ((saved_value & PUSHLOCK_FLAGS_ACQUIRED) == 0 || (saved_value & PUSHLOCK_FLAGS_EXCLUSIVE) == 0) {
			if (saved_value & PUSHLOCK_FLAGS_CONTENDED) {
				// if its contended, we will try to acquire it as an exclusive lock
				// (as we have no space for putting the shared count in the pointer)
				if (DO_CAS(pushlock, saved_value, saved_value | PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
					return true;
			} else {
				// its not contended and either unlocked or locked shared
				// set the appropriate flag and increment the shared count
				if (DO_CAS(pushlock, saved_value, PUSHLOCK_INCREMENT_SHARED_COUNT(saved_value) | PUSHLOCK_FLAGS_ACQUIRED))
					return true;
			}

			continue;
		}

		return false;
	}
}

void pushlock_acquire_shared(pushlock_t *pushlock) {
	pushlock_t saved_value = *pushlock;
	for (;;) {
		if ((saved_value & PUSHLOCK_FLAGS_ACQUIRED) == 0 || (saved_value & PUSHLOCK_FLAGS_EXCLUSIVE) == 0) {
			if (saved_value & PUSHLOCK_FLAGS_CONTENDED) {
				// if its contended, we will try to acquire it as an exclusive lock
				// (as we have no space for putting the shared count in the pointer)
				if (DO_CAS(pushlock, saved_value, saved_value | PUSHLOCK_FLAGS_ACQUIRED | PUSHLOCK_FLAGS_EXCLUSIVE))
					return;
			} else {
				// its not contended and either unlocked or locked shared
				// set the appropriate flag and increment the shared count
				if (DO_CAS(pushlock, saved_value, PUSHLOCK_INCREMENT_SHARED_COUNT(saved_value) | PUSHLOCK_FLAGS_ACQUIRED))
					return;
			}

			continue;
		}

		pushlock_wait_block_t wait_block;
		SEMAPHORE_INIT(&wait_block.semaphore, 0);

		// lock is being exclusively held, which means the bits for shared_count
		// is the pointer to the first wait block, so we can directly get it here
		wait_block.next = PUSHLOCK_GET_POINTER(saved_value);
		wait_block.shared_count = 0;

		if (DO_CAS(pushlock, saved_value, (uintptr_t)&wait_block | (saved_value & PUSHLOCK_MASK_FLAGS) | PUSHLOCK_FLAGS_CONTENDED))
			semaphore_wait(&wait_block.semaphore, false);
	}
}

void pushlock_release(pushlock_t *pushlock) {
	pushlock_t saved_value = *pushlock;
	for (;;) {
		if ((saved_value & PUSHLOCK_FLAGS_CONTENDED) == 0) {
			if ((saved_value & PUSHLOCK_FLAGS_EXCLUSIVE) == 0 && PUSHLOCK_GET_SHARED_COUNT(saved_value) > 1) {
				// shared lock with 2 or more holders, decrement shared count
				if (DO_CAS(pushlock, saved_value, PUSHLOCK_DECREMENT_SHARED_COUNT(saved_value)))
					return;
			} else {
				// shared lock with 1 holder or exclusive lock, can just set it to 0
				if (DO_CAS(pushlock, saved_value, 0))
					return;
			}

			continue;
		}

		break;
	}

	// lock is contended, we will have to go through the wait blocks and get the
	// last and penultimate ones
	bool skip_check = false;
	for (;;) {
		pushlock_wait_block_t *prev = NULL, *last = PUSHLOCK_GET_POINTER(saved_value);
		while (last->next != NULL) {
			prev = last;
			last = last->next;
		}

		// return if its shared and we arent the only holder
		if (skip_check == false && (saved_value & PUSHLOCK_FLAGS_EXCLUSIVE) == 0 && __atomic_sub_fetch(&last->shared_count, 1, __ATOMIC_SEQ_CST) > 0)
			return;

		skip_check = true;

		// here we can be assured we are the only holder
		if (prev == NULL) {
			// last is being pointed to as the front, we just have to unset it
			if (DO_CAS(pushlock, saved_value, 0) == false)
				continue;
		} else {
			// we need to remove it from prev
			prev->next = NULL;
			// and unlock it properly, keeping in mind it still contended
			for (;;) {
				if (DO_CAS(pushlock, saved_value, (uintptr_t)PUSHLOCK_GET_POINTER(saved_value) | PUSHLOCK_FLAGS_CONTENDED))
					break;
			}
		}

		// wake the waiting thread
		semaphore_signal(&last->semaphore);
		return;
	}
}
