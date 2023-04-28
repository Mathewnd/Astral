#ifndef _TIMER_H
#define _TIMER_H

#include <time.h>
#include <arch/context.h>
#include <spinlock.h>
#include <stdbool.h>

typedef struct timerentry_t {
	struct timerentry_t *next;
	time_t absolutetick;
	time_t repeatus;
	void *private;
	void (*func)(void *private, context_t* context);
} timerentry_t;

typedef struct {
	spinlock_t lock;
	time_t ticksperus;
	time_t tickcurrent;
	bool running;
	void (*arm)(time_t);
	time_t (*stop)();
	timerentry_t *queue;
} timer_t;
void timer_resume(timer_t *timer);
void timer_stop(timer_t *timer);
void timer_isr(timer_t *timer, context_t *context);
void timer_insert(timer_t *timer, timerentry_t *entry, time_t us);
timer_t *timer_new(time_t ticksperus, void (*arm)(time_t), time_t (*stop)());

#endif
