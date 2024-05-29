#include <kernel/syscalls.h>
#include <time.h>
#include <kernel/scheduler.h>
#include <logging.h>

#define ITIMER_REAL 0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF 2

typedef struct {
	timeval_t interval;
	timeval_t value;
} itimerval_t;

static itimer_t *selectitimer(int which) {
	itimer_t *itimer = NULL;
	if (which == ITIMER_REAL)
		itimer = &_cpu()->thread->proc->timer.realtime;
	else if (which == ITIMER_VIRTUAL)
		itimer = &_cpu()->thread->proc->timer.virtualtime;
	else if (which == ITIMER_PROF)
		itimer = &_cpu()->thread->proc->timer.profiling;

	return itimer;
}

syscallret_t syscall_getitimer(context_t *context, int which, itimerval_t *value) {
	syscallret_t ret = {
		.ret = -1
	};

	__assert(which == ITIMER_REAL);
	itimer_t *itimer = selectitimer(which);
	if (itimer == NULL) {
		ret.errno = EINVAL;
		return ret;
	}

	MUTEX_ACQUIRE(&_cpu()->thread->proc->timer.mutex, false);

	uintmax_t oldremaining, oldrepeat;
	itimer_pause(itimer, &oldremaining, &oldrepeat);
	if (oldremaining)
		itimer_resume(itimer);

	MUTEX_RELEASE(&_cpu()->thread->proc->timer.mutex);

	value->interval.s = oldrepeat / 1000000;
	value->interval.us = oldrepeat % 1000000;
	value->value.s = oldremaining / 1000000;
	value->value.us = oldremaining % 1000000;
	ret.errno = 0;
	return ret;
}

syscallret_t syscall_setitimer(context_t *context, int which, itimerval_t *unew, itimerval_t *uold) {
	syscallret_t ret = {
		.ret = -1
	};

	__assert(which == ITIMER_REAL);

	itimer_t *itimer = selectitimer(which);
	if (itimer == NULL) {
		ret.errno = EINVAL;
		return ret;
	}

	itimerval_t val = {0};
	if (unew)
		val = *unew;

	MUTEX_ACQUIRE(&_cpu()->thread->proc->timer.mutex, false);
	uintmax_t oldremaining, oldrepeat;
	itimer_pause(itimer, &oldremaining, &oldrepeat);

	// TODO overflow check
	uintmax_t repeatus = val.interval.s * 1000000 + val.interval.us;
	uintmax_t newus = val.value.s * 1000000 + val.value.us;
	itimer_set(itimer, newus, repeatus);

	if (newus)
		itimer_resume(itimer);

	MUTEX_RELEASE(&_cpu()->thread->proc->timer.mutex);

	if (uold) {
		uold->interval.s = oldrepeat / 1000000;
		uold->interval.us = oldrepeat % 1000000;
		uold->value.s = oldremaining / 1000000;
		uold->value.us = oldremaining % 1000000;
	}

	ret.errno = 0;
	return ret;
}
