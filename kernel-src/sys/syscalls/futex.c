#include <kernel/syscalls.h>
#include <kernel/slab.h>
#include <kernel/vmm.h>
#include <time.h>
#include <hashtable.h>
#include <kernel/poll.h>
#include <kernel/alloc.h>

static scache_t *futexcache;
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

static void ctor(scache_t *cache, void *obj) {
	futex_t *futex = obj;
	POLL_INITHEADER(&futex->pollheader);
	futex->waiting = 0;
}

syscallret_t syscall_futex(context_t *, uint32_t *futexp, int op, uint32_t value, timespec_t *tm) {
	syscallret_t ret = {
		.ret = -1
	};

	if (futexcache == NULL) {
		futexcache = slab_newcache(sizeof(thread_t), 0, ctor, ctor);

		if (futexcache == NULL) {
			ret.errno = ENOMEM;
			return ret;
		}
	}

	if (hashtableinit == false) {
		if (hashtable_init(&hashtable, 256)) {
			ret.errno = ENOMEM;
			return ret;
		}

		hashtableinit = true;
		MUTEX_INIT(&futexmutex);
	}

	timespec_t timespec = tm ? *tm : (timespec_t){0};
	polldesc_t desc = {0};
	ret.errno = poll_initdesc(&desc, 1);
	if (ret.errno)
		return ret;

	MUTEX_ACQUIRE(&futexmutex, false);

	bool doleave = true;
	uint32_t word;
	ret.errno = usercopy_fromuseratomic32(futexp, &word);
	if (ret.errno)
		goto cleanup;

	uint32_t *physical = vmm_getphysical(futexp, false);
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

			uintmax_t us = timespec.s * 1000000 + timespec.ns / 1000;
			++futex->waiting;
			
			for (;;) {
				poll_add(&futex->pollheader, &desc.data[0], POLLOUT);

				MUTEX_RELEASE(&futexmutex);

				ret.errno = poll_dowait(&desc, us);
				int revents = 0;
				if (desc.event)
					revents = desc.event->revents;

				MUTEX_ACQUIRE(&futexmutex, false);

				if (revents == 0 || ret.errno == EINTR) {
					// timed out or interrupted
					--futex->waiting;
					futex->waking = futex->waking > futex->waiting ? futex->waiting : futex->waking; // if it happened during a wakeup, just to make sure nothing bad happens
					ret.errno = ret.errno == EINTR ? EINTR : 0;
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
