#ifndef _HPET_H
#define _HPET_h

#include <stdbool.h>
#include <time.h>

time_t arch_hpet_calibrate_tsc(time_t ms_wait);

#endif
