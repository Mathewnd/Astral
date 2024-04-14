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

	if ((void *)utime > USERSPACE_END || (void *)remaining > USERSPACE_END) {
		ret.errno = EFAULT;
		return ret;
	}

	timespec_t time = *utime;

	if (time.ns > 999999999 || time.ns < 0) {
		ret.errno = EINVAL;
		return ret;
	}

	timerentry_t sleepentry;
	sched_preparesleep(true);

	sched_targetcpu(_cpu());
	timer_insert(_cpu()->timer, &sleepentry, timeout, _cpu()->thread, time.s * 1000000 + time.ns / 1000, false);

	ret.errno = sched_yield();

	if (ret.errno)
		timer_remove(_cpu()->timer, &sleepentry);

	sched_targetcpu(NULL);

	ret.ret = 0;
	return ret;
}
