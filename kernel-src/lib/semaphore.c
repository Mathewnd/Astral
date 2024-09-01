#include <semaphore.h>
#include <kernel/scheduler.h>
#include <errno.h>
#include <logging.h>

static void insert(semaphore_t *sem, thread_t *thread) {
	thread->sleepnext = sem->head;
	thread->sleepprev = NULL;
	sem->head = thread;

	if (sem->tail == NULL)
		sem->tail = thread;

	if (thread->sleepnext)
		thread->sleepnext->sleepprev = thread;
}

static thread_t *get(semaphore_t *sem) {
	thread_t *thread = sem->tail;
	if (thread == NULL)
		return NULL;

	sem->tail = thread->sleepprev;

	if (sem->tail == NULL)
		sem->head = NULL;
	else
		sem->tail->sleepnext = NULL;

	thread->sleepprev = NULL;
	thread->sleepnext = NULL;

	return thread;
}

static void removeself(semaphore_t *sem) {
	thread_t *thread = _cpu()->thread;

	// first see if we have been removed already
	if ((sem->head == NULL && sem->tail == NULL) || // no threads in sleep list
	   !((thread->sleepnext != NULL || thread->sleepprev != NULL) || // 2 or more threads in sleep list and we are one of them
	    (sem->head == thread && sem->tail == thread))) // 1 thread in the sleep list and it is us
		return;

	if (sem->head == thread) {
		sem->head = thread->sleepnext;
	} else {
		thread->sleepprev->sleepnext = thread->sleepnext;
	}

	if (sem->tail == thread) {
		sem->tail = thread->sleepprev;
	} else {
		thread->sleepnext->sleepprev = thread->sleepprev;
	}

	thread->sleepprev = NULL;
	thread->sleepnext = NULL;
}

int semaphore_wait(semaphore_t *sem, bool interruptible) {
	if (_cpu()->thread == NULL) {
		while (semaphore_test(sem) == false) CPU_PAUSE();
		return 0;
	}

	bool intstate = interrupt_set(false);
	int ret = 0;
	spinlock_acquire(&sem->lock);

	if (--sem->i < 0) {
		insert(sem, _cpu()->thread);
		sched_preparesleep(interruptible);
		spinlock_release(&sem->lock);
		ret = sched_yield();
		if (ret) {
			spinlock_acquire(&sem->lock);
			++sem->i;
			removeself(sem);
			spinlock_release(&sem->lock);
		}
		goto leave;
	}

	spinlock_release(&sem->lock);
	leave:
	interrupt_set(intstate);
	return ret;
}

int semaphore_timedwait(semaphore_t *sem, time_t timeoutusec, bool interruptible) {
	time_t sleepus;

	do {
		if (semaphore_test(sem))
			return true;

		sleepus = min(timeoutusec, 10);
		timeoutusec -= sleepus;

		if (sleepus)
			sched_sleepus(sleepus);
	} while (timeoutusec);

	return false;
}

void semaphore_signal(semaphore_t *sem) {
	bool intstate = interrupt_set(false);
	spinlock_acquire(&sem->lock);
	if (++sem->i <= 0) {
		thread_t *thread;
		do {
			// wake SOMEONE up. this will *try* to wake up a thread
			// it could wake up none though, as a thread could have been interrupted by a signal
			thread = get(sem);
			// XXX the current method doesn't respect thread priorities
			if (thread) {
				sched_wakeup(thread, 0);
				break;
			}
		} while (thread);
	}
	spinlock_release(&sem->lock);
	interrupt_set(intstate);
}

bool semaphore_haswaiters(semaphore_t *sem) {
	bool intstate = interrupt_set(false);
	spinlock_acquire(&sem->lock);

	bool v = sem->head != NULL;

	spinlock_release(&sem->lock);
	interrupt_set(intstate);
	return v;
}

bool semaphore_test(semaphore_t *sem) {
	bool intstate = interrupt_set(false);
	bool ret = false;
	spinlock_acquire(&sem->lock);

	if (sem->i > 0) {
		--sem->i;
		ret = true;
	}

	spinlock_release(&sem->lock);
	interrupt_set(intstate);
	return ret;
}
