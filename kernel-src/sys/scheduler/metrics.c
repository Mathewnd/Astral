#include <kernel/scheduler.h>
#include <kernel/timekeeper.h>
#include <util.h>

#define SCALING_FACTOR (SCHED_MAX_INTERACTIVITY / 2)

#define HISTORY_LIMIT_USEC 5000000

static void update_interactivity(thread_t *thread) {
	time_t sleep_time = thread->metrics.sleep_time_avg_us;
	time_t run_time = thread->metrics.run_time_avg_us;

	if (sleep_time > run_time)
		thread->metrics.interactivity_score = min((SCALING_FACTOR * run_time) / sleep_time, SCHED_MAX_INTERACTIVITY);
	else
		thread->metrics.interactivity_score = min((SCALING_FACTOR * sleep_time) / run_time + SCALING_FACTOR, SCHED_MAX_INTERACTIVITY);

	// TODO nice value impacting this 
}

static void offset_times(thread_t *thread, time_t run_us, time_t sleep_us) {
	thread->metrics.run_time_avg_us += run_us;
	thread->metrics.sleep_time_avg_us += sleep_us;

	time_t total = thread->metrics.run_time_avg_us + thread->metrics.sleep_time_avg_us;
	if (total > HISTORY_LIMIT_USEC) {
		if (total > HISTORY_LIMIT_USEC * 2) {
			// the thread has spent a lot of time sleeping, as this will not be reached normally otherwise
			// set its sleep time to max and zero the run time before applying normal decay
			thread->metrics.run_time_avg_us = 0;
			thread->metrics.sleep_time_avg_us = HISTORY_LIMIT_USEC;
		}
		thread->metrics.run_time_avg_us = thread->metrics.run_time_avg_us * 4 / 5;
		thread->metrics.sleep_time_avg_us = thread->metrics.sleep_time_avg_us * 4 / 5;
	}

	update_interactivity(thread);
}

void sched_thread_running_callback(thread_t *thread) {
	thread->metrics.run_start = timekeeper_time();
}

void sched_thread_stopping_callback(thread_t *thread, bool sleeping) {
	timespec_t now = timekeeper_time();
	offset_times(thread, timespec_diffus(thread->metrics.run_start, now), 0);

	if (sleeping)
		thread->metrics.sleep_start = now;
}

// called right before the specified thread gets enqueued after sleeping
void sched_thread_wakeup_callback(thread_t *thread) {
	offset_times(thread, 0, timespec_diffus(thread->metrics.sleep_start, timekeeper_time()));
}
