#include <kernel/syscalls.h>
#include <kernel/abi.h>
#include <time.h>
#include <kernel/timekeeper.h>
#include <kernel/vmm.h>

#define CLOCK_REALTIME 0
#define CLOCK_BOOTTIME 7

syscallret_t syscall_clockget(context_t *, int clockid, timespec_t *tp) {
	syscallret_t ret = {
		.ret = -1
	};

	switch (clockid) {
		case CLOCK_REALTIME: {
			timespec_t ts = timekeeper_time();
			usercopy_touser(tp, &ts, sizeof(timespec_t));
			ret.errno = 0;
			break;
		}
		case CLOCK_BOOTTIME: {
			timespec_t ts = timekeeper_timefromboot();
			usercopy_touser(tp, &ts, sizeof(timespec_t));
			ret.errno = 0;
			break;
		}
		default: {
			ret.errno = EINVAL;
			break;
		}
	}

	ret.ret = ret.errno ? -1 : 0;

	return ret;

}
