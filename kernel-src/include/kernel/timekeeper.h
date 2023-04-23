#ifndef _TIMEKEEPER_H
#define _TIMEKEEPER_H

#include <time.h>

void timekeeper_init(time_t (*tick)(), time_t ticksperus);
timespec_t timekeeper_timefromboot();
timespec_t timekeeper_time();

#endif
