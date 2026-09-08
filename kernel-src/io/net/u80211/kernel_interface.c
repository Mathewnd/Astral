#include <u80211/kernel_interface.h>

#include <kernel/alloc.h>
#include <kernel/eth.h>
#include <kernel/event.h>
#include <kernel/init.h>
#include <kernel/interrupt.h>
#include <kernel/page.h>
#include <kernel/scheduler.h>
#include <kernel/slab.h>
#include <kernel/thread.h>
#include <kernel/timekeeper.h>
#include <mutex.h>
#include <pushlock.h>
#include <semaphore.h>
#include <spinlock.h>
#include <string.h>
#include <time.h>

static scache_t *pushlock_cache;
static scache_t *semaphore_cache;
static scache_t *spinlock_cache;
static scache_t *work_cache;

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

typedef struct u80211_work_t {
	struct u80211_work_t *next;
	timespec_t deadline;
	u80211_kernel_work_fn_t function;
	void *context;
	eventheader_t finished_event;
	bool pending;
	bool running;
	bool freeing;
	bool destroy_on_return;
} u80211_work_t;

static spinlock_t work_queue_lock;
static eventheader_t work_queue_event;
static u80211_work_t *work_queue;
static u80211_work_t *current_work;
static thread_t *worker_thread;

static long acquire_work_queue_lock(void) {
	return spinlock_acquire_raise_ipl(&work_queue_lock, IPL_NET);
}

static void release_work_queue_lock(long old_ipl) {
	spinlock_release_lower_ipl(&work_queue_lock, old_ipl);
}

static timespec_t deadline_after_ms(size_t ms) {
	timespec_t delay = {
		.s = ms / 1000,
		.ns = (ms % 1000) * 1000000,
	};

	return timespec_add(timekeeper_timefromboot(), delay);
}

static void insert_work(u80211_work_t *work) {
	// insert in the ordered list
	u80211_work_t **entry = &work_queue;
	while (*entry && !timespec_bigger((*entry)->deadline, work->deadline))
		entry = &(*entry)->next;

	work->next = *entry;
	*entry = work;
	work->pending = true;
}

static bool remove_work(u80211_work_t *work) {
	// find previous in the ordered list
	u80211_work_t **entry = &work_queue;
	while (*entry && *entry != work)
		entry = &(*entry)->next;

	if (*entry == NULL)
		return false;

	*entry = work->next;
	work->next = NULL;
	work->pending = false;
	return true;
}

static time_t deadline_wait_us(timespec_t deadline, timespec_t now) {
	return (deadline.s - now.s) * 1000000 + (deadline.ns - now.ns) / 1000;
}

static void wake_work_thread(void) {
	EVENT_SIGNAL(&work_queue_event);
}

static void work_thread(void) {
	for (;;) {
		eventlistener_t listener;
		EVENT_INITLISTENER(&listener);
		EVENT_ATTACH(&listener, &work_queue_event);

		long old_ipl = acquire_work_queue_lock();
		u80211_work_t *work = work_queue;
		if (work == NULL) {
			release_work_queue_lock(old_ipl);
			EVENT_WAIT(&listener, 0);
			EVENT_DETACHALL(&listener);
			continue;
		}

		timespec_t now = timekeeper_timefromboot();
		if (timespec_bigger(work->deadline, now)) {
			time_t wait_us = deadline_wait_us(work->deadline, now);
			release_work_queue_lock(old_ipl);
			EVENT_WAIT(&listener, wait_us);
			EVENT_DETACHALL(&listener);
			continue;
		}

		work_queue = work->next;

		work->next = NULL;
		work->pending = false;
		work->running = true;

		current_work = work;

		u80211_kernel_work_fn_t function = work->function;
		void *context = work->context;

		release_work_queue_lock(old_ipl);
		EVENT_DETACHALL(&listener);

		function(context);

		old_ipl = acquire_work_queue_lock();
		current_work = NULL;
		work->running = false;
		bool destroy = work->freeing && work->destroy_on_return;
		bool notify = work->freeing && !work->destroy_on_return;
		release_work_queue_lock(old_ipl);

		if (notify)
			EVENT_SIGNAL(&work->finished_event);
		else if (destroy)
			slab_free(work_cache, work);
	}
}

void *u80211_kernel_allocate_work(void) {
	u80211_work_t *work = slab_allocate(work_cache);
	if (work) {
		memset(work, 0, sizeof(*work));
		EVENT_INITHEADER(&work->finished_event);
	}

	return work;
}

void u80211_kernel_enqueue_work(void *opaque_work, u80211_kernel_work_fn_t function, void *context, size_t ms) {
	u80211_work_t *work = opaque_work;
	timespec_t deadline = deadline_after_ms(ms);

	long old_ipl = acquire_work_queue_lock();
	if (!work->pending && !work->freeing) {
		work->deadline = deadline;
		work->function = function;
		work->context = context;

		insert_work(work);
		release_work_queue_lock(old_ipl);

		wake_work_thread();
		return;
	}

	release_work_queue_lock(old_ipl);
}

void u80211_kernel_free_work(void *opaque_work) {
	u80211_work_t *work = opaque_work;
	bool removed;
	eventlistener_t listener;
	EVENT_INITLISTENER(&listener);
	EVENT_ATTACH(&listener, &work->finished_event);

	long old_ipl = acquire_work_queue_lock();
	removed = remove_work(work);
	if (!work->running) {
		release_work_queue_lock(old_ipl);
		EVENT_DETACHALL(&listener);

		if (removed)
			wake_work_thread();

		slab_free(work_cache, work);
		return;
	}

	work->freeing = true;
	if (current_thread() == worker_thread && current_work == work) {
		work->destroy_on_return = true;
		release_work_queue_lock(old_ipl);
		EVENT_DETACHALL(&listener);

		if (removed)
			wake_work_thread();

		return;
	}

	release_work_queue_lock(old_ipl);

	if (removed)
		wake_work_thread();

	EVENT_WAIT(&listener, 0);
	EVENT_DETACHALL(&listener);
	slab_free(work_cache, work);
}

static void u80211_kernel_interface_init(void) {
	pushlock_cache = slab_newcache(sizeof(pushlock_t), 0, NULL, NULL);
	semaphore_cache = slab_newcache(sizeof(semaphore_t), 0, NULL, NULL);
	spinlock_cache = slab_newcache(sizeof(u80211_spinlock_t), 0, NULL, NULL);
	work_cache = slab_newcache(sizeof(u80211_work_t), 0, NULL, NULL);
	__assert(pushlock_cache && semaphore_cache && spinlock_cache && work_cache);

	SPINLOCK_INIT(work_queue_lock);
	EVENT_INITHEADER(&work_queue_event);

	worker_thread = sched_newthread(work_thread, PAGE_SIZE * 4, 0, NULL, NULL);
	__assert(worker_thread);
	sched_queue(worker_thread);
}

INIT_ROUTINE_DEFINE(u80211, INIT_ROUTINE_FLAGS_NONE, u80211_kernel_interface_init, scheduler);

void u80211_kernel_receive_callback(u80211_device_t *device, void *buffer, size_t size) {
	(void)size;
	eth_process(device->driver_data, buffer);
}
