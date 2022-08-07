#include <arch/schedtimer.h>
#include <arch/cls.h>
#include <arch/apic.h>

void arch_schedtimer_waitms(size_t ms){
	
	apic_timerstart(cls->schedtimerticksperms * ms);
	
}

void arch_schedtimer_stop(){

	apic_timerstop();

}

void arch_schedtimer_calibrate(){
	
	cls->schedtimerticksperms = apic_timercalibrate(1);
	
	apic_timerinterruptset(SCHEDTIMER_VECTOR);

}
