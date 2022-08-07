#ifndef _SCHEDTIMER_H_INCLUDE
#define _SCHEDTIMER_H_INCLUDE

#include <stddef.h>

#define SCHEDTIMER_VECTOR 0x50

void arch_schedtimer_run(size_t ms);
void arch_schedtimer_stop();
void arch_schedtimer_calibrate();

#endif
