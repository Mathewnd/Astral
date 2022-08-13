#include <arch/cputimer.h>
#include <arch/apic.h>
#include <arch/idt.h>

size_t arch_cputimer_init(){
	// XXX invariant TSC?
	
	size_t ticks = apic_timercalibrate(1);

	apic_timerinterruptset(VECTOR_TIMER);

	return ticks;

}

size_t arch_cputimer_stop(){
	return apic_timerstop();
}

void arch_cputimer_fire(size_t ticks){
	apic_timerstart(ticks);
}
