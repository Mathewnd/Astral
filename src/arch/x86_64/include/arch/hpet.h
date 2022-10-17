#ifndef _HPET_H_INCLUDE
#define _HPET_H_INCLUDE

#include <stddef.h>
#include <time.h>

void hpet_init();

void hpet_wait_us(size_t us);
void hpet_wait_ms(size_t ms);
void hpet_wait_s(size_t s);
time_t hpet_get_counter();
time_t hpet_get_ticksperus();

#endif
