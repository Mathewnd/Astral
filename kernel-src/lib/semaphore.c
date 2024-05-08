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

	return thread;
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
			spinlock_release(&sem->lock);
		}
		goto leave;
	}

	spinlock_release(&sem->lock);
	leave:
	interrupt_set(intstate);
	return ret;
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
