#ifndef _APIC_H_INCLUDE
#define _APIC_H_INCLUDE

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void apic_sendipi(uint8_t cpu, uint8_t vec, uint8_t dest, uint8_t mode, uint8_t level);
void apic_eoi();
size_t apic_timerstop();
void apic_timerstart(size_t ticks);
void apic_timerinterruptset(uint8_t vector);
size_t apic_timercalibrate(size_t us);
void apic_lapicinit();
void apic_init();
void ioapic_setlegacyirq(uint8_t irq, uint8_t vector, uint8_t proc, bool masked);

#endif
