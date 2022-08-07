#ifndef _HPET_H_INCLUDE
#define _HPET_H_INCLUDE

#include <stddef.h>

void hpet_init();

void hpet_wait_us(size_t us);
void hpet_wait_ms(size_t ms);
void hpet_wait_s(size_t s);

#endif
