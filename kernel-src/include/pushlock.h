#ifndef _RWLOCK_H
#define _RWLOCK_H

#include <semaphore.h>

typedef struct pushlock_wait_block_t {
	struct pushlock_wait_block_t *next;
	size_t shared_count;
	semaphore_t semaphore;
} __attribute__((aligned(8))) pushlock_wait_block_t;

typedef uintptr_t pushlock_t;

#define PUSHLOCK_FLAGS_ACQUIRED 1
#define PUSHLOCK_FLAGS_EXCLUSIVE 2
#define PUSHLOCK_FLAGS_CONTENDED 4
#define PUSHLOCK_MASK_FLAGS 0x7
#define PUSHLOCK_MASK_POINTER_SHARED_COUNT (0xfffffffffffffff8lu)

#define PUSHLOCK_GET_POINTER(x) ((pushlock_wait_block_t *)((x) & PUSHLOCK_MASK_POINTER_SHARED_COUNT))
#define PUSHLOCK_GET_SHARED_COUNT(x) ((uintptr_t)PUSHLOCK_GET_POINTER(x) >> 3)
#define PUSHLOCK_INCREMENT_SHARED_COUNT(x) ((x) + 0x8)
#define PUSHLOCK_DECREMENT_SHARED_COUNT(x) ((x) - 0x8)

bool pushlock_try_acquire_exclusive(pushlock_t *pushlock);
void pushlock_acquire_exclusive(pushlock_t *pushlock);
bool pushlock_try_acquire_shared(pushlock_t *pushlock);
void pushlock_acquire_shared(pushlock_t *pushlock);
void pushlock_release(pushlock_t *pushlock);

#endif
