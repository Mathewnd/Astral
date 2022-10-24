#include <kernel/syscalls.h>
#include <kernel/vmm.h>
#include <errno.h>
#include <kernel/event.h>
#include <arch/spinlock.h>
#include <arch/interrupt.h>
#include <kernel/alloc.h>
#include <arch/timekeeper.h>
#include <time.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

static int lock;
typedef struct _futex{
	struct _futex* next;
	void* phyaddr;
	size_t waitercount;
	size_t waitersleft;
	event_t event;
} futex_t;

static futex_t* first;

static inline futex_t* getfutex(void *addr){
	futex_t* iter = first;

	while(iter){
		if(iter->phyaddr == addr)
			break;
		iter = iter->next;
	}

	return iter;

}

static inline void setfutex(futex_t* futex){
	futex->next = first;
	first = futex;
}

syscallret syscall_futex(uint32_t *futex, int op, uint32_t v, const struct timespec* tm){

	syscallret retv;
	retv.ret = -1;

	if(futex > USER_SPACE_END || tm > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}

	uint32_t word;

	__atomic_load(futex, &word, __ATOMIC_RELAXED);

	spinlock_acquire(&lock);

	// TODO safer way of getting the address from userspace while inside the lock

	uint32_t* phyfutex = vmm_tophysical(futex);
	futex_t* f = getfutex(phyfutex);
	
	switch(op){
		case FUTEX_WAKE:
			// add to waiters left
			retv.errno = 0;

			if(!f){
				retv.ret = 0;
				break;
			}
			
			retv.ret = v > f->waitercount ? f->waitercount : v;

			
			f->waitercount -= retv.ret;
			f->waitersleft += retv.ret;

			break;			
			
				
		case FUTEX_WAIT:
			if(word != v){
				retv.errno = EAGAIN;
				break;
			}

			if(!f){
				f = alloc(sizeof(futex_t));
				if(!f){
					retv.errno = ENOMEM;
					break;
				}
				setfutex(f);
			}
			
			struct timespec target = arch_timekeeper_gettime();

			if(tm){
				target.tv_sec += tm->tv_sec;
				target.tv_nsec += tm->tv_nsec;
				if(target.tv_nsec > 1000000000){
					target.tv_sec++;
					target.tv_nsec %= 1000000000;
				}
			}

			// XXX probably should use events here

			for(;;){
				
				if(spinlock_trytoacquire(&lock)){

					if(tm != NULL){
						struct timespec now = arch_timekeeper_gettime();
						if(now.tv_sec > target.tv_sec)
                                			break;

                        			if(now.tv_sec == target.tv_sec && now.tv_nsec >= target.tv_sec)
                                			break;

					}

					if(f->waitersleft != 0){
						--f->waitersleft;
						--f->waitercount;
						break;
					}

					spinlock_release(&lock);
				}

				arch_interrupt_disable();

				sched_yield();
		
				arch_interrupt_enable();

			}
			
			retv.ret = 0;
			retv.errno = 0;
			break;
		default:
		retv.errno = ENOSYS;
	}

	spinlock_release(&lock);

	return retv;

}
