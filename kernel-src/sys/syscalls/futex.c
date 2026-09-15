#include <kernel/syscalls.h>
#include <kernel/mm.h>
#include <list.h>
#include <semaphore.h>
#include <mutex.h>

// TODO support private futexes

#define ENTRY_STATE_WAITING 0
#define ENTRY_STATE_TIMEOUT 1
#define ENTRY_STATE_AWOKEN   2

typedef struct {
	list_node_t list_node;
	uintptr_t key;
	semaphore_t semaphore;
	int state;
} futex_entry_t;

typedef struct {
	mutex_t mutex;
	list_t list;
} futex_bucket_t;

#define BUCKET_COUNT 256
static futex_bucket_t buckets[BUCKET_COUNT];

static futex_bucket_t *get_futex_bucket(uintptr_t key) {
	return &buckets[(key / 16) % 256]; // TODO proper hash
}

// expects bucket mutex held
static size_t wake_bucket(futex_bucket_t *bucket, uintptr_t key, size_t count) {
	size_t awoken = 0;

	if (count == 0)
		return 0;

	list_for_each (&bucket->list, list_node) {
		futex_entry_t *entry = (futex_entry_t *)list_node;

		if (entry->key == key) {
			int expected = ENTRY_STATE_WAITING;
			if (!__atomic_compare_exchange_n(&entry->state, &expected, ENTRY_STATE_AWOKEN, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
				continue;

			semaphore_signal(&entry->semaphore);
			++awoken;
			if (--count == 0)
				break;
		}
	}

	return awoken;
}

static void timeout(context_t *, dpcarg_t arg) {
	futex_entry_t *entry = arg;
	int expected = ENTRY_STATE_WAITING;
	if (__atomic_compare_exchange_n(&entry->state, &expected, ENTRY_STATE_TIMEOUT, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
		semaphore_signal(&entry->semaphore);
}

// expects bucket mutex to be held
// returns without the bucket held
static int wait_on_bucket(futex_bucket_t *bucket, uintptr_t key, size_t timeoutus) {
	itimer_t itimer;
	futex_entry_t entry = {
		.key = key
	};

	__atomic_store_n(&entry.state, ENTRY_STATE_WAITING, __ATOMIC_RELAXED);
	SEMAPHORE_INIT(&entry.semaphore, 0);

	list_push_back(&bucket->list, &entry.list_node);
	MUTEX_RELEASE(&bucket->mutex);

	if (timeoutus) {
		itimer_init(&itimer, timeout, &entry);
		itimer_set(&itimer, timeoutus, 0);
		itimer_resume(&itimer);
	}
	int error = semaphore_wait(&entry.semaphore, true) ? EINTR : 0;

	if (timeoutus) {
		itimer_pause(&itimer, NULL, NULL);
	}

	MUTEX_ACQUIRE(&bucket->mutex);

	int state = __atomic_load_n(&entry.state, __ATOMIC_RELAXED);
	if (state == ENTRY_STATE_AWOKEN)
		error = 0;
	else if (state == ENTRY_STATE_TIMEOUT)
		error = ETIMEDOUT;

	list_remove(&bucket->list, &entry.list_node);

	MUTEX_RELEASE(&bucket->mutex);

	if (error)
		return error;

	return 0;
}

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

syscallret_t syscall_futex(context_t *, uint32_t *futexp, int op, uint32_t value, timespec_t *tm) {
	syscallret_t ret = {
		.ret = -1
	};

	if (!IS_USER_ADDRESS(tm) || !IS_USER_ADDRESS(futexp)) {
		ret.errno = EFAULT;
		return ret;
	}

	if ((uintptr_t)futexp % 4) {
		ret.errno = EINVAL;
		return ret;
	}

	timespec_t timespec = {0};
	if (tm) {
		ret.errno = usercopy_fromuser(&timespec, tm, sizeof(timespec_t));
		if (ret.errno)
			return ret;
	}
	uintmax_t us = timespec.s * 1000000 + timespec.ns / 1000;

	uint32_t *physical = mm_get_physical_address(
		futexp,
		MM_GET_PHYSICAL_ADDRESS_FLAGS_HOLD |
		MM_GET_PHYSICAL_ADDRESS_FLAGS_LOCK |
		MM_GET_PHYSICAL_ADDRESS_FLAGS_LOCK_HINT_READ |
		MM_GET_PHYSICAL_ADDRESS_FLAGS_LOCK_FULL
	);
	if (physical == NULL) {
		ret.errno = EFAULT;
		return ret;
	}

	uintptr_t key = (uintptr_t)physical;
	uint32_t *p = MAKE_HHDM(physical);
	futex_bucket_t *bucket = get_futex_bucket(key);

	MUTEX_ACQUIRE(&bucket->mutex);
	
	switch (op) {
		case FUTEX_WAIT:
			if (__atomic_load_n(p, __ATOMIC_RELAXED) != value) {
				MUTEX_RELEASE(&bucket->mutex);
				ret.errno = EAGAIN;
				break;
			}

			if (tm && us == 0) {
				MUTEX_RELEASE(&bucket->mutex);
				ret.errno = ETIMEDOUT;
				break;
			}

			ret.errno = wait_on_bucket(bucket, key, us);
			ret.ret = ret.errno ? -1 : 0;
			break;
		case FUTEX_WAKE:
			ret.ret = wake_bucket(bucket, key, value);
			ret.errno = 0;
			MUTEX_RELEASE(&bucket->mutex);
			break;
		default:
			ret.errno = EINVAL;
			MUTEX_RELEASE(&bucket->mutex);
			break;
	}


	mm_unlock_and_release_page(physical);
	return ret;
}
