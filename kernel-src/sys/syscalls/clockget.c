#include <kernel/syscalls.h>
#include <kernel/abi.h>
#include <time.h>
#include <kernel/timekeeper.h>
#include <kernel/vmm.h>

syscallret_t syscall_clockget(context_t *, int clockid, timespec_t *tp) {
	syscallret_t ret = {
		.ret = -1
	};

	switch (clockid) {
		case CLOCK_REALTIME: {
			timespec_t ts = timekeeper_time();
			ret.errno = usercopy_touser(tp, &ts, sizeof(timespec_t));
			break;
		}
		case CLOCK_MONOTONIC:
		case CLOCK_BOOTTIME: {
			timespec_t ts = timekeeper_timefromboot();
			ret.errno = usercopy_touser(tp, &ts, sizeof(timespec_t));
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
