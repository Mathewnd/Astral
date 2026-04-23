#include <kernel/syscalls.h>
#include <time.h>
#include <kernel/usercopy.h>

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_BOOTTIME 7

syscallret_t syscall_clock_getres(context_t *, int clockid, timespec_t *res) {
	syscallret_t ret;

	switch (clockid) {
		case CLOCK_REALTIME:
		case CLOCK_MONOTONIC:
		case CLOCK_BOOTTIME:
			if (res) {
				timespec_t ts = {
					.s = 0,
					.ns = 1
				};

				ret.errno = usercopy_touser(res, &ts, sizeof(timespec_t));
			} else {
				ret.errno = 0;
			}
			break;
		default:
			ret.errno = EINVAL;
			break;
	}

	ret.ret = ret.errno ? -1 : 0;
	return ret;
}
