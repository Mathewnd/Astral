#include <kernel/syscalls.h>
#include <kernel/abi.h>
#include <time.h>
#include <kernel/timekeeper.h>
#include <kernel/vmm.h>

#define CLOCK_REALTIME 0
#define CLOCK_BOOTTIME 7

syscallret_t syscall_clockget(context_t *, int clockid, timespec_t *tp){
	syscallret_t ret = {
		.ret = -1
	};

	if ((void *)tp > USERSPACE_END) {
		ret.errno = EFAULT;
		return ret;
	}

	switch (clockid) {
		case CLOCK_REALTIME:
			// TODO safe memcpy
			*tp = timekeeper_time();
			ret.errno = 0;
			break;
		case CLOCK_BOOTTIME:
			// TODO safe memcpy
			*tp = timekeeper_timefromboot();
			ret.errno = 0;
			break;
		default:
			ret.errno = EINVAL;
			break;
	}

	ret.ret = ret.errno ? -1 : 0;

	return ret;

}
