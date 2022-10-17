#ifndef _TIMEKEEPER_H_INCLUDE
#define _TIMEKEEPER_H_INCLUDE

#include <time.h>

struct timespec arch_timekeeper_gettime();
struct timespec arch_timekeeper_gettimefromboot();

void arch_timekeeper_init();

#endif
