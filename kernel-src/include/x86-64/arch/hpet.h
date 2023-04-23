#ifndef _HPET_H
#define _HPET_h

#include <stdbool.h>
#include <time.h>

time_t arch_hpet_ticks();
void arch_hpet_waitticks(time_t ticks);
void arch_hpet_waitus(time_t us);
bool arch_hpet_exists();
time_t arch_hpet_init();

#endif
