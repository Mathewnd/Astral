#include <semaphore.h>
#include <errno.h>
#include <logging.h>

static void insert(semaphore_t *sem, thread_t *thread) {
	if (sem->head == NULL) {
		sem->head = thread;
		sem->tail = thread;
		thread->next = NULL;
		thread->prev = NULL;
		return;
	}

	sem->tail->next = _cpu()->thread;
	_cpu()->thread->prev = sem->tail;
}

static thread_t *get(semaphore_t *sem) {
	thread_t *thread = sem->head;
	__assert(thread);

	sem->head = sem->head->next;
	if (sem->head == NULL)
		sem->tail = NULL;
	else 
		sem->head->prev = NULL;

	return thread;
}

int semaphore_wait(semaphore_t *sem, bool interruptible) {
	int ret = 0;
	spinlock_acquire(&sem->lock);

	if (--sem->i < 0) {
		insert(sem, _cpu()->thread);
		sched_preparesleep(interruptible);
		spinlock_release(&sem->lock);
		return sched_yield();
	}

	spinlock_release(&sem->lock);
	return ret;
}

void semaphore_signal(semaphore_t *sem) {
	spinlock_acquire(&sem->lock);
	if (++sem->i <= 0) {
		thread_t *thread = get(sem);
		// XXX the current method doesn't respect thread priorities
		sched_wakeup(thread, 0);
	}
	spinlock_release(&sem->lock);
}

bool semaphore_test(semaphore_t *sem) {
	bool ret = false;
	spinlock_acquire(&sem->lock);

	if (sem->i > 0) {
		--sem->i;
		ret = true;
	}

	spinlock_release(&sem->lock);
	return ret;
}
