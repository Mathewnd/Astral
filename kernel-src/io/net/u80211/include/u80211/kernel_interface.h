#ifndef U80211_KERNEL_INTERFACE_H
#define U80211_KERNEL_INTERFACE_H

#include <stddef.h>
#include <u80211/u80211.h>

// allocates 'size' bytes of memory
// return NULL on allocation failure
void *u80211_kernel_allocate(size_t size);

// frees the kernel memory referenced by 'ptr'
// 'ptr' will never be NULL
void u80211_kernel_free(void *ptr);

// allocates an opaque mutex object
// return NULL on allocation failure
void *u80211_kernel_allocate_mutex(void);

// frees the mutex object associated with the 'mutex' pointer
// 'mutex' will never be NULL
void u80211_kernel_free_mutex(void *mutex);

// basic mutex operations
void u80211_kernel_acquire_mutex(void *mutex);
void u80211_kernel_release_mutex(void *mutex);

// allocates an opaque semaphore object
// 'initial_count' contains the initial value of the semaphore
void *u80211_kernel_allocate_semaphore(unsigned int initial_count);
// frees the semaphore object referenced by 'semaphore'
// 'semaphore' will never be NULL
void u80211_kernel_free_semaphore(void *semaphore);

// basic semphore operations
void u80211_kernel_wait_semaphore(void *semaphore);
void u80211_kernel_signal_semaphore(void *semaphore);

// allocates an opaque spinlock object
// return NULL on allocation failure
void *u80211_kernel_allocate_spinlock(void);

// frees the spinlock object referenced by 'spinlock'
// 'spinlock' will never be NULL
void u80211_kernel_free_spinlock(void *spinlock);

// basic spinlock operations
// in case the kernel implements interrupt priorities, these functions
// must raise/lower it from the network interrupt priority.
// otherwise, disable interrupts.
void u80211_kernel_acquire_spinlock(void *spinlock);
void u80211_kernel_release_spinlock(void *spinlock);

// allocates an opaque rwlock object
// return NULL on allocation failure
void *u80211_kernel_allocate_rwlock(void);

// frees the rwlock object referenced by 'rwlock'
// 'spinlock' will never be NULL
void u80211_kernel_free_rwlock(void *rwlock);

// basic rwlock operations
// an acquire will always be paired with a release of the same type
void u80211_kernel_acquire_rwlock_exclusive(void *rwlock);
void u80211_kernel_acquire_rwlock_shared(void *rwlock);
void u80211_kernel_release_rwlock_exclusive(void *rwlock);
void u80211_kernel_release_rwlock_shared(void *rwlock);

typedef void (*u80211_kernel_work_fn_t)(void *context);

// allocates an opaque work object
// return NULL on allocation failure
void *u80211_kernel_allocate_work(void);

// enqueue threaded work. this might be called from an interrupt context
// 'ms' == 0 enqueues the work immediatelly. otherwise, 
// work is enqueued 'ms' milliseconds in the future
// if work is already pending, the new request must be ignored
void u80211_kernel_enqueue_work(void *work, u80211_kernel_work_fn_t function, void *context, size_t ms);

// frees the work object referenced by 'work'
// 'work' will never be NULL
// may be called by the work's callback
void u80211_kernel_free_work(void *work);

// kernel packet receive callback
// buffer has an ethernet header
void u80211_kernel_receive_callback(u80211_device_t *device, void *buffer, size_t size);

#endif
