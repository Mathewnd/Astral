#ifndef _SEMAPHORE_H_INCLUDE
#define _SEMAPHORE_H_INCLUDE

#include <kernel/sched.h>
#include <kernel/alloc.h>
#include <arch/spinlock.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct{
	thread_t** waitingthreads;
	int lock, count, threadcount;
} semaphore_t;


// a thread count of 0 will be a blockless semaphore

static semaphore_t* sem_init(size_t count, size_t threadptrcount){
	
	semaphore_t* semaphore = alloc(sizeof(semaphore_t));
	if(!semaphore) return NULL;
	
	if(threadptrcount){
		semaphore->waitingthreads = alloc(sizeof(thread_t*) * threadptrcount);
		semaphore->threadcount = threadptrcount;
	}

	semaphore->count = count;

	return semaphore;
}

static bool sem_tryacquire(semaphore_t* sem){
	
	spinlock_acquire(&sem->lock);

	bool res = (sem->count > 0);

	if(res)
		--sem->count;
	

	spinlock_release(&sem->lock);

	return res;

}

static void sem_wait(semaphore_t* sem){
	
	
	spinlock_acquire(&sem->lock);
	
	--sem->count;

	if(sem->count < 0 && sem->threadcount > 0){
		// TODO block
	}
	else if(sem->threadcount == 0){
		_panic("sem_wait with threadcount 0", 0);
	}
	
	spinlock_release(&sem->lock);
	
}


static void sem_signal(semaphore_t* sem){
	
	
	spinlock_acquire(&sem->lock);
	
	++sem->count;

	if(sem->count <= 0 && sem->threadcount > 0){
		//TODO run a thread
	}
	
	spinlock_release(&sem->lock);
	
}

#endif
