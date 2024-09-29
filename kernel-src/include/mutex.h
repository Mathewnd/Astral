#ifndef _MUTEX_H
#define _MUTEX_H

#include <semaphore.h>

typedef semaphore_t mutex_t;

#define MUTEX_INIT(m) \
	SEMAPHORE_INIT(m, 1);

#define MUTEX_ACQUIRE(m, i) \
	semaphore_wait(m, i)

#define MUTEX_ACQUIRE_TIMED(m, t, i) \
	semaphore_timedwait(m, t, i)

#define MUTEX_RELEASE(m) \
	semaphore_signal(m)

#define MUTEX_TRY(m) \
	semaphore_test(m)

#define MUTEX_DEFINE(x) SEMAPHORE_DEFINE(x, 1)

#endif
