#include <u80211/kernel_interface.h>

#include <errno.h>
#include <kernel/alloc.h>
#include <kernel/eth.h>
#include <kernel/init.h>
#include <kernel/interrupt.h>
#include <kernel/itimer.h>
#include <kernel/slab.h>
#include <kernel/work.h>
#include <mutex.h>
#include <pushlock.h>
#include <semaphore.h>
#include <spinlock.h>
#include <string.h>

static scache_t *pushlock_cache;
static scache_t *semaphore_cache;
static scache_t *spinlock_cache;
static scache_t *work_cache;
static scache_t *timer_cache;

void *u80211_kernel_allocate(size_t size) {
	return alloc(size);
}

void u80211_kernel_free(void *ptr) {
	free(ptr);
}

void *u80211_kernel_allocate_mutex(void) {
	mutex_t *mutex = slab_allocate(pushlock_cache);
	if (mutex)
		MUTEX_INIT(mutex);

	return mutex;
}

void u80211_kernel_free_mutex(void *mutex) {
	slab_free(pushlock_cache, mutex);
}

void u80211_kernel_acquire_mutex(void *mutex) {
	MUTEX_ACQUIRE(mutex);
}

void u80211_kernel_release_mutex(void *mutex) {
	MUTEX_RELEASE(mutex);
}

void *u80211_kernel_allocate_semaphore(unsigned int initial_count) {
	semaphore_t *semaphore = slab_allocate(semaphore_cache);
	if (semaphore)
		SEMAPHORE_INIT(semaphore, initial_count);

	return semaphore;
}

void u80211_kernel_free_semaphore(void *semaphore) {
	slab_free(semaphore_cache, semaphore);
}

void u80211_kernel_wait_semaphore(void *semaphore) {
	semaphore_wait(semaphore, false);
}

void u80211_kernel_signal_semaphore(void *semaphore) {
	semaphore_signal(semaphore);
}

typedef struct {
	spinlock_t lock;
	long old_ipl;
} u80211_spinlock_t;


void *u80211_kernel_allocate_spinlock(void) {
	u80211_spinlock_t *spinlock = slab_allocate(spinlock_cache);
	if (spinlock)
		SPINLOCK_INIT(spinlock->lock);

	return spinlock;
}

void u80211_kernel_free_spinlock(void *spinlock) {
	slab_free(spinlock_cache, spinlock);
}

void u80211_kernel_acquire_spinlock(void *opaque_spinlock) {
	u80211_spinlock_t *spinlock = opaque_spinlock;
	spinlock->old_ipl = spinlock_acquire_raise_ipl(&spinlock->lock, IPL_NET);
}

void u80211_kernel_release_spinlock(void *opaque_spinlock) {
	u80211_spinlock_t *spinlock = opaque_spinlock;
	spinlock_release_lower_ipl(&spinlock->lock, spinlock->old_ipl);
}

void *u80211_kernel_allocate_rwlock(void) {
	pushlock_t *rwlock = slab_allocate(pushlock_cache);
	if (rwlock)
		*rwlock = 0;

	return rwlock;
}

void u80211_kernel_free_rwlock(void *rwlock) {
	slab_free(pushlock_cache, rwlock);
}

void u80211_kernel_acquire_rwlock_exclusive(void *rwlock) {
	pushlock_acquire_exclusive(rwlock);
}

void u80211_kernel_acquire_rwlock_shared(void *rwlock) {
	pushlock_acquire_shared(rwlock);
}

void u80211_kernel_release_rwlock_exclusive(void *rwlock) {
	pushlock_release_exclusive(rwlock);
}

void u80211_kernel_release_rwlock_shared(void *rwlock) {
	pushlock_release_shared(rwlock);
}

typedef struct u80211_timer u80211_timer_t;

typedef struct u80211_work {
	work_t work;
	u80211_timer_t *timer;
	u80211_kernel_work_fn_t function;
	void *context;
	bool pending;
	bool freeing;
} u80211_work_t;

struct u80211_timer {
	itimer_t timer;
	u80211_work_t *work;
};

static work_queue_t *u80211_work_queue;
static spinlock_t work_state_lock;

static long acquire_work_state_lock(void) {
	return spinlock_acquire_raise_ipl(&work_state_lock, IPL_NET);
}

static void release_work_state_lock(long old_ipl) {
	spinlock_release_lower_ipl(&work_state_lock, old_ipl);
}

static void dispatch_work(void *context, size_t) {
	u80211_work_t *work = context;
	u80211_kernel_work_fn_t function;
	void *function_context;

	long old_ipl = acquire_work_state_lock();
	work->pending = false;
	if (work->freeing) {
		release_work_state_lock(old_ipl);
		return;
	}

	function = work->function;
	function_context = work->context;
	release_work_state_lock(old_ipl);

	function(function_context);
}

static void delayed_work_timer(context_t *, dpcarg_t argument) {
	u80211_timer_t *timer = argument;

	long old_ipl = acquire_work_state_lock();

	u80211_work_t *work = timer->work;
	if (work) {
		timer->work = NULL;
		work->timer = NULL;
		if (work->pending && !work->freeing)
			work_enqueue(u80211_work_queue, &work->work);
		else
			work->pending = false;
	}

	release_work_state_lock(old_ipl);
}

void *u80211_kernel_allocate_timer(void) {
	u80211_timer_t *timer = slab_allocate(timer_cache);
	if (timer) {
		timer->work = NULL;
		itimer_init(&timer->timer, delayed_work_timer, timer);
	}

	return timer;
}

void u80211_kernel_free_timer(void *opaque_timer) {
	u80211_timer_t *timer = opaque_timer;
	itimer_pause(&timer->timer, NULL, NULL);

	long old_ipl = acquire_work_state_lock();

	u80211_work_t *work = timer->work;
	if (work && work->timer == timer) {
		work->timer = NULL;
		work->pending = false;
	}

	timer->work = NULL;

	release_work_state_lock(old_ipl);

	slab_free(timer_cache, timer);
}

void *u80211_kernel_allocate_work(void) {
	u80211_work_t *work = slab_allocate(work_cache);
	if (work) {
		memset(work, 0, sizeof(*work));
		WORK_INIT(&work->work, dispatch_work, work);
	}

	return work;
}

void u80211_kernel_enqueue_work(void *opaque_work, u80211_kernel_work_fn_t function, void *context) {
	u80211_work_t *work = opaque_work;

	long old_ipl = acquire_work_state_lock();

	if (!work->pending && !work->freeing) {
		work->function = function;
		work->context = context;
		work->pending = true;
		work_enqueue(u80211_work_queue, &work->work);
	}

	release_work_state_lock(old_ipl);
}

void u80211_kernel_enqueue_delayed_work(void *opaque_work, void *opaque_timer, u80211_kernel_work_fn_t function, void *context, size_t ms) {
	if (ms == 0) {
		u80211_kernel_enqueue_work(opaque_work, function, context);
		return;
	}

	u80211_work_t *work = opaque_work;
	u80211_timer_t *timer = opaque_timer;

	long old_ipl = acquire_work_state_lock();

	if (!work->pending && !work->freeing && timer->work == NULL) {
		work->function = function;
		work->context = context;
		work->pending = true;
		work->timer = timer;
		timer->work = work;

		itimer_set(&timer->timer, ms * 1000, 0);
		itimer_resume(&timer->timer);
	}

	release_work_state_lock(old_ipl);
}

void u80211_kernel_free_work(void *opaque_work) {
	u80211_work_t *work = opaque_work;

	long old_ipl = acquire_work_state_lock();

	work->freeing = true;
	work->pending = false;

	release_work_state_lock(old_ipl);

	while (work_dequeue(u80211_work_queue, &work->work) == EBUSY)
		work_wait(u80211_work_queue, &work->work);

	slab_free(work_cache, work);
}

static void u80211_kernel_interface_init(void) {
	pushlock_cache = slab_newcache(sizeof(pushlock_t), 0, NULL, NULL);
	semaphore_cache = slab_newcache(sizeof(semaphore_t), 0, NULL, NULL);
	spinlock_cache = slab_newcache(sizeof(u80211_spinlock_t), 0, NULL, NULL);
	work_cache = slab_newcache(sizeof(u80211_work_t), 0, NULL, NULL);
	timer_cache = slab_newcache(sizeof(u80211_timer_t), 0, NULL, NULL);
	__assert(pushlock_cache && semaphore_cache && spinlock_cache && work_cache && timer_cache);

	SPINLOCK_INIT(work_state_lock);
	u80211_work_queue = work_queue_create("u80211", 1, IPL_NET);
	__assert(u80211_work_queue);
}

INIT_ROUTINE_DEFINE(u80211, INIT_ROUTINE_FLAGS_NONE, u80211_kernel_interface_init, work_queue);

void u80211_kernel_receive_callback(u80211_device_t *device, void *buffer, size_t size) {
	(void)size;
	eth_process(device->driver_data, buffer);
}
