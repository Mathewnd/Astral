#include <kernel/syscalls.h>
#include <arch/timekeeper.h>
#include <kernel/vmm.h>
#include <time.h>
#include <errno.h>
#include <kernel/ustring.h>

#define CLOCK_REALTIME 0

syscallret syscall_clock_gettime(int clockid, struct timespec *tp){
	
	syscallret retv;
	retv.errno = 0;

	if(tp > USER_SPACE_END){
		retv.errno = EFAULT;
		retv.ret = -1;
		return retv;
	}
	
	switch(clockid){
		case CLOCK_REALTIME:
			struct timespec t = arch_timekeeper_gettime();
			retv.errno = u_memcpy(tp, &t, sizeof(struct timespec));
			break;
		default:
			retv.errno = EINVAL;

	}
	
	retv.ret = retv.errno ? -1 : 0;

	return retv;

}
