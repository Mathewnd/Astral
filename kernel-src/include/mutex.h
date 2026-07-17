#ifndef _MUTEX_H
#define _MUTEX_H

#include <pushlock.h>

typedef pushlock_t mutex_t;

#define MUTEX_INIT(m) \
	*m = 0;

#define MUTEX_ACQUIRE(m) \
	pushlock_acquire_exclusive(m)

#define MUTEX_RELEASE(m) \
	pushlock_release_exclusive(m)

#define MUTEX_TRY(m) \
	pushlock_try_acquire_exclusive(m)

#define MUTEX_DEFINE(x) pushlock_t x = 0;

#endif
