#include <kernel/timekeeper.h>
#include <arch/smp.h>
#include <arch/cpu.h>
#include <mutex.h>
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
	time_t hz = current_cpu()->timekeeper_source_info->hz;
	time_t tick_offset = current_cpu()->timekeeper_source_tick_offset;

	interrupt_loweripl(old_ipl);

	timespec_t ts;
	ticks = ticks - base_ticks + tick_offset;

	ts.s = ticks / hz;
	ts.ns = (ticks % hz) / (hz / 1000000) * 1000;

	return ts;
}

timespec_t timekeeper_time(void) {
	timespec_t fromboot = timekeeper_timefromboot();
	timespec_t unix = {
		.s = boot_unix,
		.ns = 0
	};

	return timespec_add(unix, fromboot);
}

static MUTEX_DEFINE(sync_mutex);
static volatile bool sync_go_ahead;
static volatile int cpus_waiting;
static volatile int cpus_done;
static volatile time_t sync_sec;
static volatile time_t sync_usec;

static void timekeeper_sync_isr(isr_t *, context_t *) {
	// taking interrupts here could possibly add an undetermined amount of latency, which is bad
	interrupt_set(false);

	// try to get the tick code into the cache
	current_cpu()->timekeeper_source->ticks(current_cpu()->timekeeper_source_info);
	time_t hz = current_cpu()->timekeeper_source_info->hz;
	time_t mhz = hz / 1000000;

	__atomic_fetch_add(&cpus_waiting, 1, __ATOMIC_SEQ_CST);

	while (sync_go_ahead == false) asm("");

	current_cpu()->timekeeper_source_base_ticks = current_cpu()->timekeeper_source->ticks(current_cpu()->timekeeper_source_info);
	current_cpu()->timekeeper_source_tick_offset = sync_sec * hz + sync_usec * mhz;

	__atomic_fetch_add(&cpus_done, 1, __ATOMIC_SEQ_CST);
}

// this will synchronize the timekeeper of other CPUs with the one of the calling CPU.
// all this will do will be to reset the offset and bases of each cpu timekeeper and then set
// their offsets to the passed microseconds of the calling CPU in ticks. this isn't 100% precise but its not
// exactly possible to be
void timekeeper_sync(void) {
	MUTEX_ACQUIRE(&sync_mutex, false);
	bool int_status = interrupt_set(false);

	sync_go_ahead = false;
	cpus_waiting = 0;
	cpus_done = 0;

	for (int i = 0; i < arch_smp_cpusawake; ++i) {
		if (smp_cpus[i] == current_cpu())
			continue;

		arch_smp_sendipi(smp_cpus[i], smp_cpus[i]->timekeeper_sync_isr, ARCH_SMP_IPI_TARGET, false);
	}

	// try to get things into the cache
	timekeeper_source_t *timekeeper_source = current_cpu()->timekeeper_source;
	timekeeper_source_info_t *timekeeper_source_info = current_cpu()->timekeeper_source_info;
	time_t hz = timekeeper_source_info->hz;
	time_t mhz = hz / 1000000;

	while (cpus_waiting != arch_smp_cpusawake - 1) CPU_PAUSE();

	// for real this time
	time_t ticks = timekeeper_source->ticks(timekeeper_source_info) - current_cpu()->timekeeper_source_base_ticks;
	sync_sec = ticks / hz;
	sync_usec = (ticks % hz) / mhz;
	sync_go_ahead = true;

	while (cpus_done != arch_smp_cpusawake - 1) CPU_PAUSE();

	interrupt_set(int_status);
	MUTEX_RELEASE(&sync_mutex);
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

	current_cpu()->timekeeper_sync_isr = interrupt_allocate(timekeeper_sync_isr, ARCH_EOI, IPL_DPC);
	__assert(current_cpu()->timekeeper_sync_isr);

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
	current_cpu()->timekeeper_source_tick_offset = us_offset * early_source_info->hz / 1000000;

	printf("cpu%d: timekeeper: \"%s\" selected as early source. %lu ticks at early init (%lu hz)\n",
			current_cpu_id(), early_source->name, current_cpu()->timekeeper_source_base_ticks, early_source_info->hz);
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
	time_t old_us = (old_ticks - old_base + old_tick_offset) / (old_source_info->hz / 1000000); // XXX would a division with rounding be better for precision here?
	current_cpu()->timekeeper_source_tick_offset = old_us * (new_source_info->hz / 1000000);

	interrupt_loweripl(old_ipl);

	printf("cpu%d: timekeeper: \"%s\" selected as main source. %lu ticks at init (%lu hz)\n",
			current_cpu_id(), new_source->name, current_cpu()->timekeeper_source_base_ticks, new_source_info->hz);
}
