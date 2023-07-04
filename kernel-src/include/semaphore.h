#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H

#include <stdbool.h>
#include <spinlock.h>

#define SEM_TAIL 0
#define SEM_HEAD 1

typedef struct semaphore_t{
	int i;
	spinlock_t lock;
	struct thread_t *tail;
	struct thread_t *head;
} semaphore_t;

#define SEMAPHORE_INIT(x, v) { \
		(x)->i = v; \
		SPINLOCK_INIT((x)->lock); \
		(x)->tail = NULL; \
		(x)->head = NULL; \
	}


int semaphore_wait(semaphore_t *sem, bool interruptible);
void semaphore_signal(semaphore_t *sem);
bool semaphore_test(semaphore_t *sem);
bool semaphore_haswaiters(semaphore_t *sem);

#include <kernel/scheduler.h>

#endif
