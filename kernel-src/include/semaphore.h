#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H

#include <stdbool.h>
#include <spinlock.h>
#include <kernel/scheduler.h>

#define SEM_TAIL 0
#define SEM_HEAD 1

typedef struct {
	int i;
	spinlock_t lock;
	thread_t *tail;
	thread_t *head;
} semaphore_t;

#define SEMAPHORE_INIT(x, v) { \
		(x)->val = v; \
		SPINLOCK_INIT((x)->lock); \
		(x)->list = NULL; \
	}

int semaphore_wait(semaphore_t *sem, bool interruptible);
void semaphore_signal(semaphore_t *sem);
bool semaphore_test(semaphore_t *sem);

#endif
