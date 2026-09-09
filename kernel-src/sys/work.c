#include <kernel/work.h>
#include <kernel/event.h>
#include <kernel/slab.h>
#include <kernel/init.h>
#include <kernel/alloc.h>
#include <arch/smp.h>
#include <logging.h>

static scache_t *work_queue_cache;
work_queue_t *work_sharedq;

typedef struct {
	list_node_t list_node;
	work_t *work;
	thread_t *thread;
} work_waiter_t;

typedef struct {
	list_node_t list_node;
	work_t *work;
	size_t seq;
} active_worker_t;

static bool work_active(work_queue_t *wq, work_t *work) {
	list_for_each (&wq->active_list, node) {
		active_worker_t *aw = (active_worker_t *)node;
		if (aw->work == work)
			return true;
	}

	return false;
}

static void wake_waiters(work_queue_t *wq, work_t *work) {
	list_for_each_safe (&wq->waiter_list, node) {
		work_waiter_t *ww = (work_waiter_t *)node;

		if (ww->work == work) {
			list_remove(&wq->waiter_list, &ww->list_node);
			sched_wakeup(ww->thread, SCHED_WAKEUP_REASON_NORMAL);
		}
	}
}

static work_t *find_first_with_lower_sequence(work_queue_t *wq, work_t *work, size_t seq) {
	list_for_each (&wq->active_list, node) {
		active_worker_t *aw = (active_worker_t *)node;
		if (aw->seq < seq && (work == NULL || aw->work == work))
			return aw->work;
	}

	return NULL;
}

static void worker() {
	work_queue_t *wq = current_thread()->kernelarg;
	active_worker_t aw;

	for (;;) {
		semaphore_wait(&wq->pending_work, false);

		long ipl = spinlock_acquire_raise_ipl(&wq->lock, wq->lock_ipl);

		work_t *work = (work_t *)list_pop_front(&wq->work_list);
		if (work == NULL) {
			spinlock_release_lower_ipl(&wq->lock, ipl);
			continue;
		}

		list_push_back(&wq->active_list, &aw.list_node);
		aw.work = work;
		aw.seq = wq->seq++;

		work_callback_t cb = work->callback;
		void *ctx = work->context;
		size_t pending = work->pending;

		work->pending = 0;

		spinlock_release_lower_ipl(&wq->lock, ipl);

		cb(ctx, pending);

		ipl = spinlock_acquire_raise_ipl(&wq->lock, wq->lock_ipl);
		wake_waiters(wq, work);
		list_remove(&wq->active_list, &aw.list_node);
		spinlock_release_lower_ipl(&wq->lock, ipl);
	}
}

work_queue_t *work_queue_create(const char *name, size_t thread_count, long lock_ipl) {
	work_queue_t *wq = slab_allocate(work_queue_cache);
	if (wq == NULL)
		return NULL;

	wq->threads = alloc(sizeof(thread_t *) * thread_count);
	if (wq->threads == NULL) {
		slab_free(work_queue_cache, wq);
		return NULL;
	}

	size_t threads_done;
	for (threads_done = 0; threads_done < thread_count; ++threads_done) {
		wq->threads[threads_done] = sched_newthread(worker, PAGE_SIZE * 4, 0, NULL, NULL);
		if (wq->threads[threads_done] == NULL)
			break;
	}

	if (threads_done != thread_count) {
		for (size_t i = 0; i < threads_done; ++i)
			sched_destroythread(wq->threads[i]);

		free(wq->threads);
		slab_free(work_queue_cache, wq);
		return NULL;
	}

	wq->name = name;
	SPINLOCK_INIT(wq->lock);
	list_init(&wq->work_list);
	list_init(&wq->active_list);
	list_init(&wq->waiter_list);
	SEMAPHORE_INIT(&wq->pending_work, 0);
	wq->thread_count = thread_count;
	wq->lock_ipl = lock_ipl;
	wq->seq = 0;

	for (size_t i = 0; i < thread_count; ++i) {
		wq->threads[i]->kernelarg = wq;
		sched_queue(wq->threads[i]);
	}

	return wq;
}

static void enqueue_internal(work_queue_t *wq, work_t *work) {
	if (work->pending == 0) {
		list_push_back(&wq->work_list, &work->list_node);
		semaphore_signal(&wq->pending_work);
	}

	++work->pending;
}

void work_enqueue(work_queue_t *wq, work_t *work) {
	long ipl = spinlock_acquire_raise_ipl(&wq->lock, wq->lock_ipl);

	enqueue_internal(wq, work);

	spinlock_release_lower_ipl(&wq->lock, ipl);
}

int work_dequeue(work_queue_t *wq, work_t *work) {
	long ipl = spinlock_acquire_raise_ipl(&wq->lock, wq->lock_ipl);

	int error = 0;
	if (work_active(wq, work)) {
		error = EBUSY;
		goto leave;
	}

	if (work->pending == 0)
		goto leave;

	wake_waiters(wq, work);
	list_remove(&wq->work_list, &work->list_node);
	work->pending = 0;

leave:
	spinlock_release_lower_ipl(&wq->lock, ipl);
	return error;
}

static void work_wait_active_seq(work_queue_t *wq, work_t *work, size_t seq) {
	long ipl = spinlock_acquire_raise_ipl(&wq->lock, wq->lock_ipl);

	if (!work_active(wq, work) || !find_first_with_lower_sequence(wq, work, seq)) {
		spinlock_release_lower_ipl(&wq->lock, ipl);
		return;
	}

	work_waiter_t ww;
	ww.work = work;
	ww.thread = current_thread();
	list_push_back(&wq->waiter_list, &ww.list_node);

	sched_prepare_sleep(false);
	spinlock_release_lower_ipl(&wq->lock, ipl);
	sched_yield();
}

void work_wait(work_queue_t *wq, work_t *work) {
	long ipl = spinlock_acquire_raise_ipl(&wq->lock, wq->lock_ipl);

	if (work->pending == 0 && !work_active(wq, work)) {
		spinlock_release_lower_ipl(&wq->lock, ipl);
		return;
	}

	work_waiter_t ww;
	ww.work = work;
	ww.thread = current_thread();
	list_push_back(&wq->waiter_list, &ww.list_node);

	sched_prepare_sleep(false);
	spinlock_release_lower_ipl(&wq->lock, ipl);
	sched_yield();
}

static void barrier_task(void *, size_t) {

}

void work_drain(work_queue_t *wq) {
	// enqueue a special barrier work
	work_t work;
	WORK_INIT(&work, barrier_task, current_thread());

	work_enqueue(wq, &work);
	work_wait(wq, &work);

	long ipl = spinlock_acquire_raise_ipl(&wq->lock, wq->lock_ipl);
	size_t seq = wq->seq;
	spinlock_release_lower_ipl(&wq->lock, ipl);

	// wait until every single work with a lower sequence has finished
	for (;;) {
		ipl = spinlock_acquire_raise_ipl(&wq->lock, wq->lock_ipl);
		work_t *work = find_first_with_lower_sequence(wq, NULL, seq);
		spinlock_release_lower_ipl(&wq->lock, ipl);

		if (work == NULL)
			return;

		work_wait_active_seq(wq, work, seq);
	}
}

static void work_queue_init() {
	work_queue_cache = slab_newcache(sizeof(work_queue_t), 0, NULL, NULL);
	__assert(work_queue_cache);

	work_sharedq = work_queue_create("sharedq", arch_smp_get_cpu_count(), IPL_MAX);
	__assert(work_sharedq);
}

INIT_ROUTINE_DEFINE(work_queue, INIT_ROUTINE_FLAGS_NONE, work_queue_init, scheduler);
