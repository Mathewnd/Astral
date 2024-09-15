#include <kernel/syscalls.h>
#include <time.h>
#include <kernel/vmm.h>
#include <kernel/timer.h>
#include <arch/cpu.h>

static void timeout(context_t *, dpcarg_t arg) {
	thread_t *thread = arg;
	sched_wakeup(thread, 0);
} 

syscallret_t syscall_nanosleep(context_t *, timespec_t *utime, timespec_t *remaining) {
	syscallret_t ret = {
		.ret = -1
	};

	timespec_t time;
	ret.errno = usercopy_fromuser(&time, utime, sizeof(timespec_t));
	if (ret.errno)
		return ret;

	if (time.ns > 999999999 || time.ns < 0) {
		ret.errno = EINVAL;
		return ret;
	}

	timerentry_t sleepentry;
	sched_preparesleep(true);

	sched_targetcpu(current_cpu());
	timer_insert(current_cpu()->timer, &sleepentry, timeout, current_thread(), time.s * 1000000 + time.ns / 1000, false);

	ret.errno = sched_yield() == SCHED_WAKEUP_REASON_INTERRUPTED ? EINTR : 0;

	if (ret.errno && remaining) {
		uintmax_t remainingus = timer_remove(current_cpu()->timer, &sleepentry);
		time.ns = (remainingus % 1000000) * 1000;
		time.s  = (remainingus / 1000000);
		ret.errno = usercopy_touser(remaining, &time, sizeof(timespec_t)) ? EFAULT : EINTR;
	}

	sched_targetcpu(NULL);

	ret.ret = 0;
	return ret;
}
