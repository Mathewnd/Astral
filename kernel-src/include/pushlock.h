#ifndef _PUSHLOCK_H
#define _PUSHLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include <kernel/piab.h>

typedef uintptr_t pushlock_t;

bool pushlock_try_acquire_exclusive(pushlock_t *pushlock);
bool pushlock_try_acquire_shared(pushlock_t *pushlock);

void pushlock_acquire_exclusive(pushlock_t *pushlock);
void pushlock_acquire_shared(pushlock_t *pushlock);

void pushlock_release_exclusive(pushlock_t *pushlock);
void pushlock_release_shared(pushlock_t *pushlock);

#define PUSHLOCK_FLAGS_EXCLUSIVE 1u
#define PUSHLOCK_FLAGS_DISABLE_PIAB_TRACKING 2u
#define PUSHLOCK_VALID_FLAGS (PUSHLOCK_FLAGS_EXCLUSIVE | PUSHLOCK_FLAGS_DISABLE_PIAB_TRACKING)

//
// All acquisition forms track through PIAB by default. The _flags forms select
// shared or exclusive acquisition and can disable tracking. Flags of zero mean
// shared acquisition with tracking.
//
// Release uses the same flags as acquisition.
//
// Acquisition returns 0 on success, EINVAL for unknown flags, or EBUSY when
// a try_acquire fails.
//
int pushlock_acquire_flags(pushlock_t *pushlock, unsigned flags);
int pushlock_try_acquire_flags(pushlock_t *pushlock, unsigned flags);
void pushlock_release_flags(pushlock_t *pushlock, unsigned flags);

//
// The _fast forms return the PIAB record to the caller. Pass that record back
// to release_fast to avoid searching the thread's records by lock address.
//
// A successful acquisition may return NULL when tracking is disabled or its
// cache is full. Pass NULL back too.
//
// A failed acquisition needs no release.
//
int pushlock_acquire_fast(pushlock_t *pushlock, unsigned flags, piab_record_t **record);
int pushlock_try_acquire_fast(pushlock_t *pushlock, unsigned flags, piab_record_t **record);
void pushlock_release_fast(pushlock_t *pushlock, unsigned flags, piab_record_t *record);

#endif
