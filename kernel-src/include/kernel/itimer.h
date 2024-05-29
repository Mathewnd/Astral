#ifndef _ITIMER_H
#define _ITIMER_H

#include <kernel/dpc.h>
#include <kernel/timer.h>

typedef struct {
	struct cpu_t *cpu;
	timerentry_t entry;
	uintmax_t remainingus;
	uintmax_t repeatus;
	dpcfn_t fn;
	dpcarg_t arg;
	bool paused;
	spinlock_t lock;
} itimer_t;

void itimer_init(itimer_t *itimer, dpcfn_t fn, dpcarg_t arg);
void itimer_pause(itimer_t *itimer, uintmax_t *remainingus, uintmax_t *repeatus);
void itimer_set(itimer_t *itimer, uintmax_t timerus, uintmax_t repeatus);
void itimer_resume(itimer_t *itimer);

#endif
