#ifndef _TIMER_H
#define _TIMER_H

#include <time.h>
#include <arch/context.h>
#include <spinlock.h>
#include <stdbool.h>
#include <kernel/dpc.h>

typedef struct timerentry_t {
	struct timerentry_t *next;
	time_t absolutetick;
	time_t repeatus;
	dpcfn_t fn;
	dpcarg_t arg;
	dpc_t dpc;
	bool fired;
} timerentry_t;

typedef struct timer_t {
	spinlock_t lock;
	time_t tick_limit;
	time_t ticksperus;
	time_t tickcurrent;
	time_t current_target;
	bool running;
	void (*arm)(struct timer_t *, time_t);
	time_t (*stop)(struct timer_t *);
	time_t (*time_passed)(struct timer_t *);
	timerentry_t *queue;
} timer_t;

time_t timer_get_ticks(timer_t *timer);
void timer_resume(timer_t *timer);
void timer_stop(timer_t *timer);
void timer_isr(timer_t *timer, context_t *context);
void timer_insert(timer_t *timer, timerentry_t *entry, dpcfn_t fn, dpcarg_t arg, time_t us, bool repeating);
time_t timer_remove(timer_t *timer, timerentry_t *entry);
timer_t *timer_new(time_t ticksperus, void (*arm)(timer_t *, time_t), time_t (*stop)(timer_t *), time_t (*time_passed)(timer_t *), time_t tick_limit);

#endif
