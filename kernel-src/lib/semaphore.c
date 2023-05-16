#include <semaphore.h>
#include <errno.h>
#include <logging.h>

static void insert(semaphore_t *sem, thread_t *thread) {
	if (sem->head == NULL) {
		sem->head = thread;
		sem->tail = thread;
		thread->sleepnext = NULL;
		thread->sleepprev = NULL;
		return;
	}

	sem->tail->sleepnext = _cpu()->thread;
	_cpu()->thread->sleepprev = sem->tail;
}

static thread_t *get(semaphore_t *sem) {
	thread_t *thread = sem->head;
	__assert(thread);

	sem->head = sem->head->sleepnext;
	if (sem->head == NULL)
		sem->tail = NULL;
	else 
		sem->head->sleepprev = NULL;

	return thread;
}

int semaphore_wait(semaphore_t *sem, bool interruptible) {
	bool intstate = interrupt_set(false);
	int ret = 0;
	spinlock_acquire(&sem->lock);

	if (--sem->i < 0) {
		insert(sem, _cpu()->thread);
		sched_preparesleep(interruptible);
		spinlock_release(&sem->lock);
		return sched_yield();
	}

	spinlock_release(&sem->lock);
	interrupt_set(intstate);
	return ret;
}

void semaphore_signal(semaphore_t *sem) {
	bool intstate = interrupt_set(false);
	spinlock_acquire(&sem->lock);
	if (++sem->i <= 0) {
		thread_t *thread = get(sem);
		// XXX the current method doesn't respect thread priorities
		sched_wakeup(thread, 0);
	}
	spinlock_release(&sem->lock);
	interrupt_set(intstate);
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
