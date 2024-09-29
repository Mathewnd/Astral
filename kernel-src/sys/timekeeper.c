#include <kernel/timekeeper.h>
#include <limine.h>
#include <logging.h>

static volatile struct limine_boot_time_request timereq = {
	.id = LIMINE_BOOT_TIME_REQUEST,
	.revision = 0
};

static time_t boot_unix;

timespec_t timekeeper_timefromboot(void) {
	long old_ipl = interrupt_raiseipl(IPL_DPC);

	time_t ticks = current_cpu()->timekeeper_source->ticks(current_cpu()->timekeeper_source_info);
	time_t base_ticks = current_cpu()->timekeeper_source_base_ticks;
	time_t ticks_per_us = current_cpu()->timekeeper_source_info->ticks_per_us;
	time_t tick_offset = current_cpu()->timekeeper_source_tick_offset;

	interrupt_loweripl(old_ipl);

	timespec_t ts;
	ticks = ticks - base_ticks + tick_offset;
	time_t us_passed = ticks / ticks_per_us;

	ts.s = us_passed / 1000000;
	ts.ns = (us_passed * 1000) % 1000000000;

	return ts;
}

timespec_t timekeeper_time() {
	timespec_t fromboot = timekeeper_timefromboot();
	timespec_t unix = {
		.s = boot_unix,
		.ns = 0
	};

	return timespec_add(unix, fromboot);
}

extern timekeeper_source_t *timekeeper_sources;
extern timekeeper_source_t *timekeeper_sources_end;

static timekeeper_source_t *get_source(int priority, bool early) {
	timekeeper_source_t *to_use = NULL;

	for (timekeeper_source_t **sourcep = &timekeeper_sources; sourcep < &timekeeper_sources_end; ++sourcep) {
		timekeeper_source_t *source = *sourcep;
		if (source->priority <= priority || (early && (source->flags & TIMEKEEPER_SOURCE_FLAGS_EARLY) == 0) || source->probe() == false)
			continue;

		to_use = source;
		priority = source->priority;
	}

	return to_use;
}

void timekeeper_wait_us(time_t us) {
	timespec_t target = timespec_add(timespec_from_us(us), timekeeper_timefromboot());
	while (timespec_bigger(target, timekeeper_timefromboot()))
		CPU_PAUSE();
}

void timekeeper_early_init(time_t us_offset) {
	__assert(timereq.response);
	boot_unix = timereq.response->boot_time;

	timekeeper_source_t *early_source = get_source(-1, true);
	if (early_source == NULL) {
		// we cannot continue without any timekeeper source
		__assert(!"Unable to find an early timekeeper source");
	}

	timekeeper_source_info_t *early_source_info = early_source->init();
	__assert(early_source_info);

	// no need to raise IPL, this is called before the scheduler is initialised.
	current_cpu()->timekeeper_source_base_ticks = early_source->ticks(early_source_info);
	current_cpu()->timekeeper_source_info = early_source_info;
	current_cpu()->timekeeper_source = early_source;

	// this tick offset is to account for any possible time before early_init.
	// for example, on SMP init the early init will have the time passed on the ap as an offset
	current_cpu()->timekeeper_source_tick_offset = us_offset * early_source_info->ticks_per_us;

	printf("cpu%d: timekeeper: \"%s\" selected as early source. %lu ticks at early init (%lu ticks per us)\n",
			current_cpu_id(), early_source->name, current_cpu()->timekeeper_source_base_ticks, early_source_info->ticks_per_us);
}

// this will do a re-init per cpu
// the initialization threads will not switch cpus so we dont have to worry about it here
void timekeeper_init(void) {
	timekeeper_source_t *old_source = current_cpu()->timekeeper_source;
	timekeeper_source_info_t *old_source_info = current_cpu()->timekeeper_source_info;

	timekeeper_source_t *new_source = get_source(old_source->priority, false);
	if (new_source == old_source || new_source == NULL) {
		// there is no better source in the system available than the current one
		printf("cpu%d: timekeeper: keeping early source as main source\n", current_cpu_id());
		return;
	}

	timekeeper_source_info_t *new_source_info = new_source->init();
	__assert(new_source_info);

	// we do need to raise IPL to IPL_DPC to not get preempted here as the
	// timekeeper data will be in an invalid state
	long old_ipl = interrupt_raiseipl(IPL_DPC);

	time_t old_base = current_cpu()->timekeeper_source_base_ticks;

	current_cpu()->timekeeper_source_base_ticks = new_source->ticks(new_source_info);
	current_cpu()->timekeeper_source_info = new_source_info;
	current_cpu()->timekeeper_source = new_source;

	// we need a way to account for the time between early init and init in the main source
	// we will calculate how much time passed and have a tick offset
	time_t old_tick_offset = current_cpu()->timekeeper_source_tick_offset;
	time_t old_ticks = old_source->ticks(old_source_info);
	time_t old_us = (old_ticks - old_base + old_tick_offset) / old_source_info->ticks_per_us; // XXX would a division with rounding be better for precision here?
	current_cpu()->timekeeper_source_tick_offset = old_us * new_source_info->ticks_per_us;

	interrupt_loweripl(old_ipl);

	printf("cpu%d: timekeeper: \"%s\" selected as main source. %lu ticks at init (%lu ticks per us)\n",
			current_cpu_id(), new_source->name, current_cpu()->timekeeper_source_base_ticks, new_source_info->ticks_per_us);
}
