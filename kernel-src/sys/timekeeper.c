#include <kernel/timekeeper.h>
#include <limine.h>
#include <logging.h>

static volatile struct limine_boot_time_request timereq = {
	.id = LIMINE_BOOT_TIME_REQUEST,
	.revision = 0
};

static time_t bootunix;
static time_t ticksperus;
static time_t (*clockticks)();
static time_t initclockticks;

timespec_t timekeeper_timefromboot() {
	timespec_t ts;
	time_t ticks = clockticks() - initclockticks;
	time_t uspassed = ticks / ticksperus;
	ts.s = uspassed / 1000000;
	ts.ns = (uspassed * 1000) % 1000000000;
	return ts;
}

timespec_t timekeeper_time() {
	timespec_t fromboot = timekeeper_timefromboot();
	timespec_t unix;
	unix.s = bootunix;
	unix.ns = 0;
	return timespec_add(unix, fromboot);
}

// tick is a function that returns the amount of ticks since the timer's initialisation
void timekeeper_init(time_t (*tick)(), time_t _ticksperus) {
	__assert(timereq.response);
	bootunix = timereq.response->boot_time;
	printf("timekeeper: unix time at boot: %lu\n", bootunix);
	initclockticks = tick();
	ticksperus = _ticksperus;
	clockticks = tick;
	printf("timekeeper: %lu clock ticks at init (%lu ticks per us)\n", initclockticks, ticksperus);
}
