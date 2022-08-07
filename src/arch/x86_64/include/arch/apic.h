#ifndef _APIC_H_INCLUDE
#define _APIC_H_INCLUDE

#include <stddef.h>
#include <stdint.h>

void apic_timerstop();
void apic_timerstart(size_t ticks);
void apic_timerinterruptset(uint8_t vector);
size_t apic_timercalibrate(size_t ms);
void apic_lapicinit();
void apic_init();

#endif
