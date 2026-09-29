//
// Priority Inversion Avoidance Boosting (PIAB) facility
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// PIAB needs to support push locks, which also back Astral's mutexes. A push
// lock fits in one word and keeps wait blocks on the waiting threads' kernel
// stacks. It has neither an owner pointer nor a separate block shared by
// owners and waiters where we could keep priority inheritance data. We keep
// a record for each tracked acquisition in the owning thread instead. Other
// lock types can use these records without changing their wait queues. Records
// are grouped by lock address, with separate trees for owners and waiters.
//
// Lock order is record -> partition lock -> thread's priority_lock. The
// partition lock protects the resource head, both trees, and their priority
// keys. Updating another owner's floor takes priority_lock, not its record
// lock. Taking that record lock here would reverse the order used by
// registration and release.
//
// Following a chain while holding these locks would require nesting partition
// locks. Instead, floor changes queue threads for another pass after the locks
// are dropped. Scheduler priority changes use a per processor DPC list because a
// device wakeup can interrupt code holding a record or partition lock.
//

#include "piab_internal.h"
#include <kernel/scheduler.h>
#include <kernel/alloc.h>
#include <kernel/dpc.h>
#include <arch/cpu.h>
#include <logging.h>
#include <errno.h>
#include <rbtree.h>
#include <semaphore.h>

#define PIAB_HASH_BUCKETS 64

static struct {
	spinlock_t lock;
	rbtree_t *resources;
} piab_buckets[PIAB_HASH_BUCKETS];

_Static_assert(PIAB_HASH_BUCKETS > 0 && (PIAB_HASH_BUCKETS & (PIAB_HASH_BUCKETS - 1)) == 0,
	"PIAB hash bucket count must be a power of two");

_Static_assert(PIAB_RECORD_COUNT > 0 && PIAB_RECORD_COUNT < 32,
	"PIAB record count must fit the slot bitmaps");

_Static_assert(sizeof(piab_records_t) <= 1024,
	"PIAB record array exceeds 1 KiB");

_Static_assert(sizeof(piab_thread_state_t) <= 1024,
	"PIAB initial allocation exceeds 1 KiB");

#ifdef PIAB_RECORD_TEST_HOOKS
void piab_test_preparing_record(piab_record_t *record);
void piab_test_pending_record(piab_record_t *record);
void piab_test_returning_record(piab_record_t *record);
#endif

static void piab_lock_record_at_dpc(piab_record_t *record) {
	__assert(current_cpu()->ipl == IPL_DPC);

	while (__atomic_test_and_set(&record->busy, __ATOMIC_ACQUIRE)) {
		while (__atomic_load_n(&record->busy, __ATOMIC_RELAXED))
			CPU_PAUSE();
	}
}

static long piab_lock_record(piab_record_t *record) {
	long ipl = interrupt_raiseipl(IPL_DPC);
	piab_lock_record_at_dpc(record);
	return ipl;
}

static void piab_unlock_record(piab_record_t *record, long ipl) {
	__atomic_clear(&record->busy, __ATOMIC_RELEASE);
	interrupt_loweripl(ipl);
}

static void piab_update_thread(thread_t *thread, thread_t **boost_list);

// Queue thread for a priority update. Hold priority_lock so two callers
// can't link it at once through boost_next.
static void piab_queue_thread(thread_t *thread, thread_t **boost_list) {
	piab_thread_state_t *piab = thread->piab_state;

	if (piab->queued)
		return;

	__assert(piab->references != ~0u);
	++piab->references;
	piab->queued = true;
	piab->boost_next = *boost_list;
	*boost_list = thread;
}

// Update priorities for the queued threads. Record and partition locks must
// be dropped before calling here, since the update may follow other locks.
// Unlink each thread first so priority changes during the update can queue
// it again and keep its reference until the CPU queue update has finished.
static void piab_propagate_boosts(thread_t **boost_list) {
	thread_t *thread;
	piab_thread_state_t *piab;
	semaphore_t *reference_waiter;
	bool irq;

	while ((thread = *boost_list) != NULL) {
		piab = thread->piab_state;
		irq = spinlock_acquire_irq_clear(&thread->priority_lock);
		*boost_list = piab->boost_next;
		piab->boost_next = NULL;
		piab->queued = false;
		spinlock_release_irq_restore(&thread->priority_lock, irq);

		piab_update_thread(thread, boost_list);
		sched_priority_changed(thread);

		// Dropping the last reference lets thread destruction free piab and
		// thread. Neither may be read after releasing priority_lock
		irq = spinlock_acquire_irq_clear(&thread->priority_lock);
		__assert(piab->references);
		reference_waiter = NULL;
		if (--piab->references == 0) {
			reference_waiter = piab->reference_waiter;
			piab->reference_waiter = NULL;
		}
		spinlock_release_irq_restore(&thread->priority_lock, irq);

		if (reference_waiter)
			semaphore_signal(reference_waiter);
	}
}

// Process pending threads after the wakeup caller has dropped its locks.
// Device ISRs can queue more threads while this DPC walks records, so leave
// those threads for the next invocation.
static void piab_process_priority_changes(context_t *context, dpcarg_t arg) {
	thread_t *boost_list;
	bool irq;

	(void)context;
	(void)arg;

	irq = interrupt_set(false);
	boost_list = current_cpu()->piab_pending_threads;
	current_cpu()->piab_pending_threads = NULL;
	interrupt_set(irq);

	piab_propagate_boosts(&boost_list);
}

void piab_init_cpu(void) {
	current_cpu()->piab_pending_threads = NULL;
	dpc_prepare(&current_cpu()->piab_priority_dpc, piab_process_priority_changes);
}

// Defer priority updates to a DPC queued on the current CPU. Record and
// partition locks are taken at IPL_DPC, so a device interrupt can interrupt
// a holder. (!) Taking those locks here could deadlock. It also keeps the tree
// walk out of the caller's locked section.
void piab_thread_priority_changed(thread_t *thread) {
	piab_thread_state_t *piab;
	bool irq;

	// Registration increments the count before sampling priority. If this
	// load sees 0, registration must see the preceding priority store.
	piab = __atomic_load_n(&thread->piab_state, __ATOMIC_SEQ_CST);
	if (piab == NULL || __atomic_load_n(&piab->registered_count, __ATOMIC_SEQ_CST) == 0)
		return;

	irq = spinlock_acquire_irq_clear(&thread->priority_lock);
	piab_queue_thread(thread, &current_cpu()->piab_pending_threads);
	spinlock_release(&thread->priority_lock);

	// Keep IRQs off until enqueue so neither migration nor the local DPC
	// can change which CPU owns this list
	if (current_cpu()->piab_pending_threads)
		dpc_enqueue(&current_cpu()->piab_priority_dpc, NULL);
	interrupt_set(irq);
}

// Update the boost held by one record and account for it in the thread's floor.
// Called with the partition lock held. Each record accounts for its own floor,
// even when another record already gives this thread the same effective priority.
// Otherwise releasing either lock could remove the other lock's boost.
//
// Don't take the owner's record lock here. Registration and release take
// that lock before the partition lock, so taking it here would deadlock...
static void piab_set_record_boost(piab_record_t *record,
	sched_priority_t priority, thread_t **boost_list) {
	thread_t *thread = record->records->thread;
	bool irq;

	if (record->boost == priority)
		return;

	irq = spinlock_acquire_irq_clear(&thread->priority_lock);
	if (sched_replace_priority_floor_locked(thread, record->boost, priority))
		piab_queue_thread(thread, boost_list);
	record->boost = priority;
	spinlock_release_irq_restore(&thread->priority_lock, irq);
}

// The head is embedded in a registered record. Its lock address stays unchanged
// until release removes or replaces the head under the partition lock.
static int piab_compare_resource_key(void *key, rbtree_t *node) {
	piab_resource_t *resource = container_of(node, piab_resource_t, node);
	uintptr_t a = (uintptr_t)key;
	piab_record_t *record = container_of(resource, piab_record_t, head);
	uintptr_t b = (uintptr_t)record->lock;

	return (a > b) - (a < b);
}

static int piab_compare_resources(rbtree_t *a, rbtree_t *b) {
	piab_resource_t *resource = container_of(a, piab_resource_t, node);
	piab_record_t *record = container_of(resource, piab_record_t, head);

	return piab_compare_resource_key((void *)record->lock, b);
}

// The caller holds the partition lock until it has finished using the head
static piab_resource_t *piab_find_resource(size_t bucket, const void *lock) {
	rbtree_t *node = rbtree_lookup(piab_buckets[bucket].resources, (void *)lock, piab_compare_resource_key);
	return node ? container_of(node, piab_resource_t, node) : NULL;
}

static size_t piab_bucket_index(const void *lock) {
	uintptr_t value = (uintptr_t)lock >> 3;
	return (value ^ (value >> 8)) & (PIAB_HASH_BUCKETS - 1);
}

// Keep equal priority records distinct by ordering them by address
static int piab_compare_records(rbtree_t *a, rbtree_t *b) {
	piab_record_t *left = container_of(a, piab_record_t, node);
	piab_record_t *right = container_of(b, piab_record_t, node);
	uintptr_t x = (uintptr_t)left, y = (uintptr_t)right;

	if (left->priority != right->priority)
		return left->priority < right->priority ? -1 : 1;
	return (x > y) - (x < y);
}

// ACQUIRING and RETRYING belong to the owner tree too. A woken waiter can
// acquire before its post_acquire runs and keeping it in the waiter tree would
// hide that owner from a thread which blocks in the meantime.
static rbtree_t **piab_record_tree(piab_resource_t *resource, piab_record_state_t state) {
	return state == PIAB_RECORD_WAITING ? &resource->waiters : &resource->owners;
}

// Called with the partition lock held. Equal priorities are ordered by record
// address, so changing a priority requires removal and reinsertion.
static void piab_insert_record(piab_resource_t *resource, piab_record_t *record,
	piab_record_state_t state) {
	thread_t *thread = record->records->thread;

	if (state == PIAB_RECORD_WAITING)
		record->priority = sched_thread_priority(thread);
	else
		record->priority = max(record->boost, __atomic_load_n(&thread->base_priority, __ATOMIC_SEQ_CST));

	rbtree_insert(piab_record_tree(resource, state), &record->node, piab_compare_records);
}

// Boost owners below the highest waiter priority. Owners are ordered by
// max(base priority, this record's boost). Another lock's boost must not
// make us skip an owner which still needs this lock's boost.
static void piab_boost_owners(piab_resource_t *resource, thread_t **boost_list) {
	rbtree_t *node, *next;
	piab_record_t *record;
	sched_priority_t priority;

	if (resource->waiters == NULL || resource->owners == NULL)
		return;

	node = rbtree_last(resource->waiters);
	priority = container_of(node, piab_record_t, node)->priority;
	for (node = rbtree_first(resource->owners); node; node = next) {
		record = container_of(node, piab_record_t, node);
		if (record->priority >= priority)
			break;
		next = rbtree_successor(node);
		piab_set_record_boost(record, priority, boost_list);
		rbtree_remove(&resource->owners, node);
		piab_insert_record(resource, record, __atomic_load_n(&record->state, __ATOMIC_ACQUIRE));
	}
}

// Change state with the record and partition locks held.
static void piab_change_record_state(piab_record_t *record, piab_record_state_t state,
	thread_t **boost_list) {
	piab_resource_t *resource;
	piab_record_state_t old = __atomic_load_n(&record->state, __ATOMIC_ACQUIRE);

	resource = piab_find_resource(piab_bucket_index(record->lock), record->lock);
	__assert(resource);

	// Remove the node before changing state, the old state tells us which tree contains it
	rbtree_remove(piab_record_tree(resource, old), &record->node);

	// A retry must not pass its old owner boost back to the same lock
	if (state == PIAB_RECORD_WAITING)
		piab_set_record_boost(record, 0, boost_list);
	__atomic_store_n(&record->state, state, __ATOMIC_RELEASE);
	piab_insert_record(resource, record, state);
	piab_boost_owners(resource, boost_list);
}

// Update a thread's registered priority keys and queue any resulting boosts.
// Arrays remain linked until destruction, but a registered bit snapshot
// can outlive a record being released and reused. Recheck registered under
// the record lock before using its lock address.
static void piab_update_thread(thread_t *thread, thread_t **boost_list) {
	piab_thread_state_t *piab;
	piab_records_t *records;
	piab_record_t *record;
	piab_resource_t *resource;
	piab_record_state_t state;
	const void *lock;
	size_t bucket;
	sched_priority_t priority;
	unsigned registered, index;
	long ipl;

	piab = __atomic_load_n(&thread->piab_state, __ATOMIC_SEQ_CST);
	if (piab == NULL)
		return;

	records = __atomic_load_n(&piab->records, __ATOMIC_SEQ_CST);
	for (; records; records = records->next) {
		registered = __atomic_load_n(&records->registered, __ATOMIC_SEQ_CST);
		while (registered) {
			index = __builtin_ctz(registered);
			registered &= registered - 1;
			record = &records->slots[index];
			ipl = piab_lock_record(record);
			if (!record->registered) {
				piab_unlock_record(record, ipl);
				continue;
			}
			lock = record->lock;
			bucket = piab_bucket_index(lock);
			spinlock_acquire(&piab_buckets[bucket].lock);
			state = __atomic_load_n(&record->state, __ATOMIC_ACQUIRE);

			if (state == PIAB_RECORD_WAITING)
				priority = sched_thread_priority(thread);
			else
				priority = max(record->boost, __atomic_load_n(&thread->base_priority, __ATOMIC_SEQ_CST));

			if (record->priority != priority) {
				resource = piab_find_resource(bucket, lock);
				__assert(resource);
				rbtree_remove(piab_record_tree(resource, state), &record->node);
				piab_insert_record(resource, record, state);
				piab_boost_owners(resource, boost_list);
			}
			spinlock_release(&piab_buckets[bucket].lock);
			piab_unlock_record(record, ipl);
		}
	}
}

// Update registered keys after changing the thread's priority. The caller
// keeps the thread alive and holds neither its priority lock nor a cpu queue
// lock. Propagation may update other threads before this call returns.
void piab_update_thread_records(thread_t *thread) {
	thread_t *boost_list = NULL;

	piab_update_thread(thread, &boost_list);
	piab_propagate_boosts(&boost_list);
}

// Allocate a record array outside lock acquisition and scheduler callbacks.
static __attribute__((cold, noinline)) int piab_allocate_records(thread_t *thread) {
	piab_thread_state_t *piab = thread->piab_state;
	piab_records_t *records;
	unsigned i;

	__assert(current_cpu()->ipl == IPL_NORMAL);

	if (piab == NULL) {
		piab = alloc(sizeof(*piab));
		if (piab == NULL)
			return ENOMEM;
		memset(piab, 0, sizeof(*piab));
		records = &piab->initial_records;
	} else {
		records = alloc(sizeof(*records));
		if (records == NULL)
			return ENOMEM;
		memset(records, 0, sizeof(*records));
	}

	records->thread = thread;
	records->available = PIAB_RECORD_MASK;
	for (i = 0; i < PIAB_RECORD_COUNT; ++i)
		records->slots[i].records = records;

	// next never changes after publication, remote priority updates may
	// walk these arrays until the last thread reference is dropped
	records->next = piab->records;
	__atomic_store_n(&piab->records, records, __ATOMIC_SEQ_CST);
	records->available_next = piab->available;
	piab->available = records;
	if (thread->piab_state == NULL)
		__atomic_store_n(&thread->piab_state, piab, __ATOMIC_SEQ_CST);
	return 0;
}

// Reserve capacity before a thread starts, or from that thread at normal IPL.
int piab_reserve_records(thread_t *thread, unsigned count) {
	piab_records_t *records;
	unsigned capacity = 0;
	int error;

	if (thread->piab_state) {
		for (records = thread->piab_state->records; records; records = records->next)
			capacity += PIAB_RECORD_COUNT;
	}

	while (capacity < count) {
		error = piab_allocate_records(thread);
		if (error)
			return error;
		capacity += PIAB_RECORD_COUNT;
	}
	return 0;
}

// Supply the initial pool of records before the thread can acquire tracked locks.
int piab_thread_init(thread_t *thread) {
	__assert(thread->piab_state == NULL);
	return piab_reserve_records(thread, PIAB_RECORD_COUNT);
}

// Return a record to the owning thread's available slots.
// The owning thread has cleared the pending bit and removed any registration.
// Switch out must no longer be able to register this slot before it's reused.
static void piab_return_record(piab_record_t *record) {
	piab_records_t *records = record->records;
	thread_t *thread = records->thread;
	unsigned index = record - records->slots;

	__assert(index < PIAB_RECORD_COUNT && !(records->available & (1u << index)));
	__assert(!record->registered);
	// Keep this load separate from busy which a stale bitmap reader may
	// change while registered is clear
	__assert(__atomic_load_n(&record->boost, __ATOMIC_RELAXED) == 0);
	__atomic_store_n(&record->lock, NULL, __ATOMIC_RELEASE);
	__atomic_store_n(&record->state, PIAB_RECORD_FREE, __ATOMIC_RELEASE);

	if (records->available == 0) {
		records->available_next = thread->piab_state->available;
		thread->piab_state->available = records;
	}

	records->available |= 1u << index;
}

// Prepare tracking before touching the lock. The record remains valid through
// post_release or cancellation.
// A NULL result permits acquisition without tracking.
piab_record_t *piab_pre_acquire(const void *lock, bool exclusive) {
	thread_t *thread = current_thread();
	piab_thread_state_t *piab;
	piab_records_t *records;
	piab_record_t *record;
	unsigned index;
	long ipl;

	// TODO: init the boot thread before taking tracked locks
	if (thread == NULL)
		return NULL;
	__assert(current_cpu()->ipl == IPL_NORMAL);
	piab = thread->piab_state;
	if (piab == NULL)
		return NULL;
	records = piab->available;
	if (records == NULL)
		return NULL;

	__assert(records->available);
	index = __builtin_ctz(records->available);
	records->available &= ~(1u << index);
	if (records->available == 0) {
		piab->available = records->available_next;
		records->available_next = NULL;
	}
	record = &records->slots[index];

	// The pending bit is still clear. Switch out can't register this slot,
	// and stale bitmap readers skip it while registered is clear.
	__assert(!record->registered && record->lock == NULL);
	record->exclusive = exclusive;
	__atomic_store_n(&record->state, PIAB_RECORD_ACQUIRING, __ATOMIC_RELEASE);
	__atomic_store_n(&record->lock, lock, __ATOMIC_RELEASE);
#ifdef PIAB_RECORD_TEST_HOOKS
	piab_test_preparing_record(record);
#endif
	__atomic_fetch_or(&records->pending, 1u << index, __ATOMIC_RELEASE);
#ifdef PIAB_RECORD_TEST_HOOKS
	piab_test_pending_record(record);
#endif

	// Switch out may have removed this array before the bit was set.
	// Recheck the list at DPC level only when the array needs linking.
	if (!__atomic_load_n(&records->pending_queued, __ATOMIC_ACQUIRE)) {
		ipl = interrupt_raiseipl(IPL_DPC);
		if (!__atomic_load_n(&records->pending_queued, __ATOMIC_RELAXED)) {
			records->pending_next = piab->pending;
			piab->pending = records;
			__atomic_store_n(&records->pending_queued, true, __ATOMIC_RELEASE);
		}
		interrupt_loweripl(ipl);
	}

	return record;
}

// Change the acquiring thread's record between ACQUIRING and WAITING.
// Exclude switch out while checking registered, then move the tree node if
// registration has already occurred.
static void piab_set_record_state(piab_record_t *record, piab_record_state_t state) {
	thread_t *boost_list = NULL;
	piab_record_state_t old;
	size_t bucket;
	long ipl;

	if (record == NULL)
		return;

	__assert(record->records->thread == current_thread());

	ipl = interrupt_raiseipl(IPL_DPC);
	old = __atomic_load_n(&record->state, __ATOMIC_ACQUIRE);

	if (state == PIAB_RECORD_WAITING) {
		__assert(old == PIAB_RECORD_ACQUIRING || old == PIAB_RECORD_RETRYING || old == PIAB_RECORD_HELD);
	} else {
		__assert(state == PIAB_RECORD_ACQUIRING && old == PIAB_RECORD_WAITING);
	}

	if (!record->registered) {
		__atomic_store_n(&record->state, state, __ATOMIC_RELEASE);
	} else {
		bucket = piab_bucket_index(record->lock);
		piab_lock_record_at_dpc(record);
		spinlock_acquire(&piab_buckets[bucket].lock);
		piab_change_record_state(record, state, &boost_list);
		spinlock_release(&piab_buckets[bucket].lock);
		__atomic_clear(&record->busy, __ATOMIC_RELEASE);
	}

	piab_propagate_boosts(&boost_list);
	interrupt_loweripl(ipl);
}

// Record that the acquisition succeeded.
// ACQUIRING and RETRYING already use the owner tree and its priority key.
// Registration and priority updates can observe either state without rekeying.
void piab_post_acquire(piab_record_t *record) {
	piab_record_state_t state;

	if (record == NULL) {
		// Only a successful untracked acquisition permits a missing record
		// at release. A failed try must not disable that check.
		if (current_thread())
			current_thread()->piab_tracking_exhausted = true;
		return;
	}

	__assert(record->records->thread == current_thread());
	state = __atomic_load_n(&record->state, __ATOMIC_ACQUIRE);
	__assert(state == PIAB_RECORD_ACQUIRING || state == PIAB_RECORD_RETRYING);
	__atomic_store_n(&record->state, PIAB_RECORD_HELD, __ATOMIC_RELEASE);
}

// Prepare the record for another acquisition attempt. This doesn't acquire
// the lock. The caller must no longer be queued for the previous attempt.
void piab_prepare_retry(piab_record_t *record) {
	piab_record_state_t state;

	if (record == NULL)
		return;

	__assert(record->records->thread == current_thread());

	state = __atomic_load_n(&record->state, __ATOMIC_ACQUIRE);
	__assert(state == PIAB_RECORD_ACQUIRING || state == PIAB_RECORD_RETRYING ||
		state == PIAB_RECORD_WAITING || state == PIAB_RECORD_HELD);
	if (state == PIAB_RECORD_ACQUIRING)
		return;

	// Aborting queue insertion leaves WAITING behind, only that case needs
	// a tree move (and pre_wakeup already moved a RETRYING record)
	if (state == PIAB_RECORD_WAITING)
		piab_set_record_state(record, PIAB_RECORD_ACQUIRING);
	else
		__atomic_store_n(&record->state, PIAB_RECORD_ACQUIRING, __ATOMIC_RELEASE);
}

// Mark an acquisition as waiting so it can boost the lock's owners.
// Call before making the waiter visible to a waker. If queue insertion fails,
// call piab_prepare_retry since registration may already have put it in the wait tree.
void piab_pre_wait(piab_record_t *record) {
	piab_set_record_state(record, PIAB_RECORD_WAITING);
}

// Move a woken waiter back to the owner tree before signaling it.
// The waiter may acquire it and be preempted before post_acquire,
// so it must already be able to receive a boost. After signaling,
// it may immediately wait again using this same record.
void piab_pre_wakeup(piab_record_t *record) {
	thread_t *boost_list = NULL;
	size_t bucket;
	long ipl;

	if (record == NULL)
		return;

	ipl = piab_lock_record(record);
	__assert(__atomic_load_n(&record->state, __ATOMIC_ACQUIRE) == PIAB_RECORD_WAITING);
	if (record->registered) {
		bucket = piab_bucket_index(record->lock);
		spinlock_acquire(&piab_buckets[bucket].lock);
		piab_change_record_state(record, PIAB_RECORD_RETRYING, &boost_list);
		spinlock_release(&piab_buckets[bucket].lock);
	} else {
		__atomic_store_n(&record->state, PIAB_RECORD_RETRYING, __ATOMIC_RELEASE);
	}
	piab_unlock_record(record, ipl);

	piab_propagate_boosts(&boost_list);
}

// Remove a registered owner and return its slot. Move an embedded resource
// head to a surviving record before this one can be reused.
static __attribute__((noinline)) void piab_free_registered_record(piab_record_t *record) {
	thread_t *boost_list = NULL;
	thread_t *thread = current_thread();
	piab_resource_t *resource, *replacement;
	piab_record_t *successor;
	rbtree_t *node;
	size_t bucket;
	unsigned registered;
	long ipl;

	ipl = interrupt_raiseipl(IPL_DPC);
	piab_lock_record_at_dpc(record);
	bucket = piab_bucket_index(record->lock);
	spinlock_acquire(&piab_buckets[bucket].lock);
	resource = piab_find_resource(bucket, record->lock);
	__assert(resource);

	// Remove tree membership before the slot can be reused
	rbtree_remove(&resource->owners, &record->node);
	record->registered = false;
	registered = __atomic_fetch_sub(&thread->piab_state->registered_count, 1, __ATOMIC_SEQ_CST);
	__assert(registered != 0);
	__atomic_fetch_and(&record->records->registered,
		~(1u << (record - record->records->slots)), __ATOMIC_SEQ_CST);

	if (resource->owners == NULL && resource->waiters == NULL) {
		rbtree_remove(&piab_buckets[bucket].resources, &resource->node);
	} else if (resource == &record->head) {
		// The head can't outlive this record. All head access is under
		// the partition lock, including release of the successor.
		node = resource->waiters ? resource->waiters : resource->owners;
		successor = container_of(node, piab_record_t, node);
		replacement = &successor->head;
		__assert(successor != record && successor->registered);
		__assert(successor->lock == record->lock);

		replacement->owners = resource->owners;
		replacement->waiters = resource->waiters;
		// Both heads have the same lock address, so the tree order is unchanged
		rbtree_replace(&piab_buckets[bucket].resources, &resource->node, &replacement->node);
	}
	piab_set_record_boost(record, 0, &boost_list);
	spinlock_release(&piab_buckets[bucket].lock);

	piab_return_record(record);
	piab_unlock_record(record, ipl);

	piab_propagate_boosts(&boost_list);
}

// Release a record after unlock or a failed acquisition, on its own thread.
// Most records never reach a tree and can be returned without taking a lock.
static void piab_free_record(piab_record_t *record) {
	// Clear pending before checking registered. A switch either registered
	// this record already, or can no longer put it in a tree. Keep the array
	// on the pending list until switch out, even when its bitmap is empty.
	__atomic_fetch_and(&record->records->pending, ~(1u << (record - record->records->slots)), __ATOMIC_ACQ_REL);
#ifdef PIAB_RECORD_TEST_HOOKS
	piab_test_returning_record(record);
#endif
	if (record->registered)
		piab_free_registered_record(record);
	else
		piab_return_record(record);
}

// Remove tracking after unlock. The record must remain an owner until release,
// including when the releasing thread is preempted.
void piab_post_release_fast(const void *lock, bool exclusive, piab_record_t *record) {
	thread_t *thread = current_thread();

	if (thread == NULL)
		return;
	__assert(current_cpu()->ipl == IPL_NORMAL);
	if (record == NULL) {
		__assert(thread->piab_tracking_exhausted);
		return;
	}

	__assert(record->records->thread == thread);
	__assert(record->lock == lock && record->exclusive == exclusive);
	__assert(__atomic_load_n(&record->state, __ATOMIC_ACQUIRE) == PIAB_RECORD_HELD);
	piab_free_record(record);
}

// Find the released lock in this thread's records.
void piab_post_release(const void *lock, bool exclusive) {
	thread_t *thread = current_thread();
	piab_records_t *records;
	piab_record_t *record;
	unsigned occupied, index;

	if (thread == NULL)
		return;
	__assert(current_cpu()->ipl == IPL_NORMAL);
	records = NULL;
	if (thread->piab_state)
		records = thread->piab_state->records;
	for (; records; records = records->next) {
		occupied = PIAB_RECORD_MASK & ~records->available;
		while (occupied) {
			index = __builtin_ctz(occupied);
			occupied &= occupied - 1;
			record = &records->slots[index];
			if (record->lock == lock && record->exclusive == exclusive &&
				__atomic_load_n(&record->state, __ATOMIC_ACQUIRE) == PIAB_RECORD_HELD) {
				piab_post_release_fast(lock, exclusive, record);
				return;
			}
		}
	}

	__assert(thread->piab_tracking_exhausted);
}

// Abandon an acquisition which never obtained the lock
void piab_cancel_acquire(piab_record_t *record) {
	if (record == NULL)
		return;

	__assert(record->records->thread == current_thread());
	__assert(__atomic_load_n(&record->state, __ATOMIC_ACQUIRE) == PIAB_RECORD_ACQUIRING);
	piab_free_record(record);
}

// Register one pending record during switch out. The record lock
// excludes remote wakeup while choosing the tree.
static void piab_register_record(piab_record_t *record, thread_t **boost_list) {
	piab_resource_t *head;
	size_t bucket = piab_bucket_index(record->lock);
	unsigned index = record - record->records->slots;
	long ipl;

	ipl = piab_lock_record(record);
	__assert(!record->registered);
	__assert(__atomic_load_n(&record->state, __ATOMIC_ACQUIRE) != PIAB_RECORD_FREE);
	spinlock_acquire(&piab_buckets[bucket].lock);
	head = piab_find_resource(bucket, record->lock);
	if (head == NULL) {
		head = &record->head;
		head->owners = NULL;
		head->waiters = NULL;
		rbtree_insert(&piab_buckets[bucket].resources, &head->node, piab_compare_resources);
	}
	// Publish the count and bit before sampling priority. A priority updater
	// either sees this registration OR its store precedes our priority load.
	__atomic_fetch_add(&record->records->thread->piab_state->registered_count, 1, __ATOMIC_SEQ_CST);
	__atomic_fetch_or(&record->records->registered, 1u << index, __ATOMIC_SEQ_CST);
	piab_insert_record(head, record, __atomic_load_n(&record->state, __ATOMIC_ACQUIRE));
	record->registered = true;
	piab_boost_owners(head, boost_list);
	spinlock_release(&piab_buckets[bucket].lock);
	piab_unlock_record(record, ipl);
}

// Register pending acquisitions before the current thread stops running.
// Only this thread changes its pending list.
// N.B. Remote wakeup can change a record's state, so registration reads it
// 		while holding the record lock.
void piab_pre_switch(void) {
	thread_t *boost_list = NULL;
	thread_t *thread = current_thread();
	piab_records_t *records, *next;
	unsigned pending, index;

	if (thread == NULL || thread->piab_state == NULL)
		return;

	records = thread->piab_state->pending;
	thread->piab_state->pending = NULL;
	for (; records; records = next) {
		next = records->pending_next;
		records->pending_next = NULL;
		__atomic_store_n(&records->pending_queued, false, __ATOMIC_RELEASE);
		pending = __atomic_exchange_n(&records->pending, 0, __ATOMIC_ACQUIRE);
		while (pending) {
			index = __builtin_ctz(pending);
			pending &= pending - 1;
			piab_register_record(&records->slots[index], &boost_list);
		}
	}

	piab_propagate_boosts(&boost_list);
}

// Check whether the thread has returned all of its tracking records.
// Called by that thread or after it has stopped.
bool piab_thread_is_clear(thread_t *thread) {
	piab_records_t *records;

	if (thread->piab_state == NULL)
		return true;
	for (records = thread->piab_state->records; records; records = records->next) {
		if (records->available != PIAB_RECORD_MASK)
			return false;
	}
	return true;
}

// Wait for outstanding priority updates and free the thread's record arrays.
// N.B. No new tracking operations may start on this thread, but a propagation pass
// 		may still hold a reference after its last record was released.
void piab_thread_destroy(thread_t *thread) {
	piab_thread_state_t *piab = thread->piab_state;
	semaphore_t reference_waiter;
	bool irq, wait_for_updates;
	piab_records_t *records, *next;
	piab_record_t *record;

	if (piab == NULL)
		return;

	__assert(piab_thread_is_clear(thread));
	__assert(__atomic_load_n(&piab->registered_count, __ATOMIC_RELAXED) == 0);
	// queued is cleared before a pass runs, references also counts passes
	// still using the records or updating the CPU queue
	SEMAPHORE_INIT(&reference_waiter, 0);
	irq = spinlock_acquire_irq_clear(&thread->priority_lock);
	wait_for_updates = piab->references != 0;
	if (wait_for_updates) {
		__assert(piab->reference_waiter == NULL);
		piab->reference_waiter = &reference_waiter;
	}
	spinlock_release_irq_restore(&thread->priority_lock, irq);
	if (wait_for_updates)
		(void)semaphore_wait(&reference_waiter, false);

	for (records = piab->records; records; records = next) {
		next = records->next;
		__assert(__atomic_load_n(&records->registered, __ATOMIC_RELAXED) == 0);
		for (unsigned i = 0; i < PIAB_RECORD_COUNT; ++i) {
			record = &records->slots[i];
			__assert(!record->registered);
			__assert(record->state == PIAB_RECORD_FREE && record->boost == 0);
		}
		if (records != &piab->initial_records)
			free(records);
	}
	thread->piab_state = NULL;
	free(piab);
}

// Inspect the registered graph.
// A record can change between ACQUIRING/HELD/RETRYING here, but can't change trees
// while the partition lock is held.
void piab_get_lock_state(const void *lock, piab_lock_state_t *state) {
	size_t bucket = piab_bucket_index(lock);
	bool irq = spinlock_acquire_irq_clear(&piab_buckets[bucket].lock);

	piab_resource_t *resource = piab_find_resource(bucket, lock);

	memset(state, 0, sizeof(*state));
	if (resource) {
		for (unsigned tree = 0; tree < 2; ++tree) {
			rbtree_t *root = tree ? resource->waiters : resource->owners;
			rbtree_t *node;

			rbtree_check(root);
			for (node = root ? rbtree_first(root) : NULL; node;
				node = rbtree_successor(node)) {
				piab_record_t *record = container_of(node, piab_record_t, node);
				piab_record_state_t record_state;

				record_state = __atomic_load_n(&record->state, __ATOMIC_ACQUIRE);
				__assert(record->registered && record->lock == lock);
				__assert((record_state == PIAB_RECORD_WAITING) == (tree == 1));
				switch (record_state) {
					case PIAB_RECORD_HELD:
						++state->owners;
						state->shared_owners += !record->exclusive;
						break;
					case PIAB_RECORD_WAITING:
						++state->waiting;
						break;
					case PIAB_RECORD_RETRYING:
						++state->retrying;
						break;
					case PIAB_RECORD_ACQUIRING:
						++state->acquiring;
						break;
					default:
						__assert(false);
				}
			}
		}
	}
	spinlock_release_irq_restore(&piab_buckets[bucket].lock, irq);
}
