#ifndef _PUSHLOCK_H
#define _PUSHLOCK_H

#include <stdint.h>
#include <stdbool.h>

typedef uintptr_t pushlock_t;

bool pushlock_try_acquire_exclusive(pushlock_t *pushlock);
bool pushlock_try_acquire_shared(pushlock_t *pushlock);

void pushlock_acquire_exclusive(pushlock_t *pushlock);
void pushlock_acquire_shared(pushlock_t *pushlock);

void pushlock_release_exclusive(pushlock_t *pushlock);
void pushlock_release_shared(pushlock_t *pushlock);

#endif
