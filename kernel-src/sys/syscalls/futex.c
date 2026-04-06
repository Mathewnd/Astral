#include <kernel/syscalls.h>
#include <kernel/slab.h>
#include <kernel/vmm.h>
#include <time.h>
#include <hashtable.h>
#include <kernel/poll.h>
#include <kernel/alloc.h>
#include <arch/cpu.h>

static mutex_t futexmutex;
static hashtable_t hashtable;
static bool hashtableinit;

typedef struct {
	pollheader_t pollheader;
	int waiting;
	int waking;
} futex_t;

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

static futex_t *getfutex(void *physical) {
	void *tmp;
	if (hashtable_get(&hashtable, &tmp, &physical, sizeof(physical)))
		tmp = NULL;

	return tmp;
}

static int setfutex(futex_t *futex, void *physical) {
	return hashtable_set(&hashtable, futex, &physical, sizeof(physical), true);
}

static void removefutex(void *physical) {
	hashtable_remove(&hashtable, &physical, sizeof(physical));
}

syscallret_t syscall_futex(context_t *, uint32_t *futexp, int op, uint32_t value, timespec_t *tm) {
	syscallret_t ret = {
		.ret = -1
	};

	if (!IS_USER_ADDRESS(tm) || !IS_USER_ADDRESS(futexp)) {
		ret.errno = EFAULT;
		return ret;
	}

	if (unlikely(hashtableinit == false)) {
		if (unlikely(hashtable_init(&hashtable, 256))) {
			ret.errno = ENOMEM;
			return ret;
		}

		hashtableinit = true;
		MUTEX_INIT(&futexmutex);
	}

	timespec_t timespec = {0};
	if (tm) {
		ret.errno = usercopy_fromuser(&timespec, tm, sizeof(timespec_t));
		if (ret.errno)
			return ret;
	}
	uintmax_t us = timespec.s * 1000000 + timespec.ns / 1000;

	polldesc_t desc = {0};
	ret.errno = poll_initdesc(&desc, 1);
	if (unlikely(ret.errno))
		return ret;

	MUTEX_ACQUIRE(&futexmutex);

	bool doleave = true;
	uint32_t word;
	ret.errno = usercopy_fromuseratomic32(futexp, &word);
	if (unlikely(ret.errno))
		goto cleanup;

	uint32_t *physical = vmm_getphysical(futexp, 0);
	futex_t *futex = getfutex(physical);

	switch (op) {
		case FUTEX_WAKE:
			ret.errno = 0;
			if (futex == NULL) {
				ret.ret = 0;
				break;
			}

			int delta = futex->waiting - futex->waking;
			ret.ret = value > delta ? delta : value;

			futex->waking += ret.ret;

			poll_event(&futex->pollheader, POLLOUT);

			break;
		case FUTEX_WAIT:
			if (word != value) {
				ret.errno = EAGAIN;
				break;
			}

			// if timeout is zero, do not sleep
			if (tm && us == 0) {
				ret.errno = ETIMEDOUT;
				break;
			}

			if (futex == NULL) {
				futex = alloc(sizeof(futex_t));
				if (futex == NULL) {
					ret.errno = ENOMEM;
					break;
				}

				ret.errno = setfutex(futex, physical);
				if (ret.errno)
					break;
			}

			++futex->waiting;

			for (;;) {
				poll_add(&futex->pollheader, &desc.data[0], POLLOUT);

				MUTEX_RELEASE(&futexmutex);

				ret.errno = poll_dowait(&desc, us);

				MUTEX_ACQUIRE(&futexmutex);

				if (ret.errno) {
					// timed out or interrupted
					--futex->waiting;
					futex->waking = futex->waking > futex->waiting ? futex->waiting : futex->waking; // if it happened during a wakeup, just to make sure nothing bad happens
				} else if (futex->waking == 0) {
					// should go back to sleep
					poll_leave(&desc);
					continue;
				} else {
					// can leave normally!
					--futex->waiting;
					--futex->waking;
					ret.errno = 0;
				}

				// clean up if needed
				if (futex->waiting == 0) {
					doleave = false;
					free(futex);
					removefutex(physical);
				}

				ret.ret = 0;
				break;
			}
			break;
			default:
			ret.errno = ENOSYS;
	}

	cleanup:
	if (doleave)
		poll_leave(&desc);

	poll_destroydesc(&desc);

	MUTEX_RELEASE(&futexmutex);
	return ret;
}
