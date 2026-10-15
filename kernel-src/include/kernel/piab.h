#ifndef _PIAB_H
#define _PIAB_H

#include <stdbool.h>

struct thread_t;
typedef struct piab_record piab_record_t;

//
// PIAB tracking API for lock implementations.
//

//
// Uses cached records and never allocates, including for try acquisition.
//
// Returns NULL before thread setup or when no record is available.
// Acquisition may proceed, but an untracked owner can't receive a boost
// through this lock.
//
piab_record_t *piab_pre_acquire(const void *lock, bool exclusive);

//
// Call after success even if pre_acquire returned NULL, so release can
// distinguish untracked acquisitions from missing records.
//
void piab_post_acquire(piab_record_t *record);

//
// Undo pre_wait after failed waiter insertion or start another attempt
// after wakeup. The record must no longer be queued on the lock.
//
void piab_prepare_retry(piab_record_t *record);

//
// Cancel an unsuccessful attempt after removing its waiter, if any, and
// calling prepare_retry. No cancellation is permitted after lock acquisition.
//
void piab_cancel_acquire(piab_record_t *record);

//
// Call before publishing the waiter. The waker calls pre_wakeup before
// signaling it, while the waiter and its record are still valid.
//
void piab_pre_wait(piab_record_t *record);
void piab_pre_wakeup(piab_record_t *record);

//
// Address-based release searches this thread's records. The _fast form
// uses the record returned by pre_acquire, including NULL.
//
void piab_post_release(const void *lock, bool exclusive);
void piab_post_release_fast(const void *lock, bool exclusive, piab_record_t *record);

//
// PIAB support for dispatcher and thread management.
//

int piab_thread_init(struct thread_t *thread);
void piab_init_cpu(void);
void piab_thread_priority_changed(struct thread_t *thread);
void piab_pre_switch(void);
void piab_update_thread_records(struct thread_t *thread);
bool piab_thread_is_clear(struct thread_t *thread);
void piab_thread_destroy(struct thread_t *thread);

#endif // _PIAB_H
