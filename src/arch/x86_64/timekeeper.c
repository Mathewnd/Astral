#include <arch/timekeeper.h>
#include <limine.h>
#include <arch/panic.h>
#include <arch/hpet.h>

static time_t bootunixtime;
static time_t timerticksperus;
static time_t timerusatboot;

static volatile struct limine_boot_time_request lim_boottime = {
	.id = LIMINE_BOOT_TIME_REQUEST,
	.revision = 0,
};

struct timespec arch_timekeeper_gettimefromboot(){
	struct timespec time;

	time_t timercurrentus = hpet_get_counter() / timerticksperus - timerusatboot;

	time.tv_sec = timercurrentus / US_IN_SECS;
	time.tv_nsec = (timercurrentus % US_IN_SECS) * NS_IN_US;

	return time;

}

struct timespec arch_timekeeper_gettime(){
	struct timespec time;
	struct timespec timefromboot = arch_timekeeper_gettimefromboot();
	time.tv_sec = bootunixtime + timefromboot.tv_sec;
	time.tv_nsec = timefromboot.tv_nsec;

	return time;

}

void arch_timekeeper_init(){
	
	if(!lim_boottime.response)
		_panic("No limine boot time response\n", 0);
	
	bootunixtime = lim_boottime.response->boot_time;

	timerticksperus  = hpet_get_ticksperus();
	
	timerusatboot = hpet_get_counter() / timerticksperus;
}
