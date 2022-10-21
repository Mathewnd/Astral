#include <kernel/syscalls.h>
#include <time.h>
#include <arch/timekeeper.h>
#include <kernel/sched.h>
#include <arch/interrupt.h>

// XXX proper implementation and not a hacky stub

syscallret syscall_nanosleep(struct timespec *time, struct timespec *remaining){
	
	syscallret retv;
	retv.ret = -1;

	if(time > USER_SPACE_END || remaining > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}

	struct timespec t = *time;

	if(t.tv_nsec > 999999999 || t.tv_nsec < 0){
		retv.errno = EINVAL;
		return retv;
	}

	struct timespec start = arch_timekeeper_gettime();
	struct timespec end = start;
	end.tv_sec += t.tv_sec;
	end.tv_nsec += t.tv_nsec;

	if(end.tv_nsec / 999999999){
		++end.tv_sec;
		end.tv_nsec %= 999999999;
	}

	for(;;){
		
		struct timespec current = arch_timekeeper_gettime();
		
		if(current.tv_sec > end.tv_sec)
			break;

		if(current.tv_sec == end.tv_sec && current.tv_nsec > end.tv_nsec)
			break;
		
		arch_interrupt_disable();

		sched_yield();

		arch_interrupt_enable();
		
	}
	
	remaining->tv_nsec = 0;
	remaining->tv_sec = 0;
	
	retv.errno = 0;
	retv.ret = 0;

	return retv;


}
