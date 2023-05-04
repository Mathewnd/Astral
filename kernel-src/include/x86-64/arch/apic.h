#ifndef _APIC_H
#define _APIC_H

void arch_apic_timerinit();
void arch_apic_init();
void arch_apic_initap();
void arch_apic_eoi();
void arch_ioapic_setirq(uint8_t gsi, uint8_t vector, uint8_t proc, bool masked);

#endif
