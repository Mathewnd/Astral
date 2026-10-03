#ifndef _PIAB_INTERNAL_H
#define _PIAB_INTERNAL_H

#include <kernel/piab.h>
#include <kernel/scheduler.h>
#include <rbtree.h>
#include <stdint.h>
#include <stddef.h>

struct piab_records;
typedef struct piab_resource {
	rbtree_t node;
	rbtree_t *owners;
	rbtree_t *waiters;
} piab_resource_t;

typedef enum {
	PIAB_RECORD_FREE,
	PIAB_RECORD_ACQUIRING,
	PIAB_RECORD_HELD,
	PIAB_RECORD_WAITING,
	PIAB_RECORD_RETRYING
} piab_record_state_t;

struct piab_record {
	struct piab_records *records;
	rbtree_t node;				// Partition lock
	const void *lock;
	sched_priority_t priority;	// Partition lock
	sched_priority_t boost;		// Partition lock and thread's priority lock
	uint8_t state;
	bool exclusive;
	// Registration changes hold busy and the partition lock. Readers
	// need either lock, except the owning thread before registration.
	bool registered;
	bool busy;
	piab_resource_t head;
};

#define PIAB_RECORD_COUNT 8
#define PIAB_RECORD_MASK ((1u << PIAB_RECORD_COUNT) - 1)

typedef struct piab_records {
	struct piab_records *next;
	struct piab_records *available_next;
	struct piab_records *pending_next;
	thread_t *thread;
	unsigned available;
	unsigned pending;	// Interlocked against switch out registration
	unsigned registered;
	bool pending_queued;
	piab_record_t slots[PIAB_RECORD_COUNT];
} piab_records_t;

// Allocated with the first record array and retained until thread destruction.
// Queued update references keep this state and all record arrays alive.
typedef struct piab_thread_state {
	piab_records_t *records;		// Interlocked publication, immutable next links
	piab_records_t *available;
	piab_records_t *pending;
	struct thread_t *boost_next;	// Thread's priority lock
	struct semaphore_t *reference_waiter; // Thread's priority lock
	unsigned references;			// Thread's priority lock
	unsigned registered_count;		// Interlocked
	unsigned waiter_count;			// Interlocked
	bool queued;
	bool base_changed;				// Thread's priority lock
	piab_records_t initial_records;
} piab_thread_state_t;

typedef struct {
	size_t owners;
	size_t shared_owners;
	size_t waiting;
	size_t retrying;
	size_t acquiring;
} piab_lock_state_t;

int piab_reserve_records(thread_t *thread, unsigned count);

void piab_get_lock_state(const void *lock, piab_lock_state_t *state);

#endif // _PIAB_INTERNAL_H
