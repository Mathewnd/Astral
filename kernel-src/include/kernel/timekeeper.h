#ifndef _TIMEKEEPER_H
#define _TIMEKEEPER_H

#include <time.h>
#include <stdbool.h>

#define TIMEKEEPER_SOURCE_FLAGS_EARLY 1 // can be used as an early timer

typedef struct {
	time_t ticks_per_us;
	void *private;
} timekeeper_source_info_t;

typedef struct {
	const char *name;
	int priority;
	bool (*probe)(void); // returns true if the timer can currently be used
	timekeeper_source_info_t *(*init)(void); // initializes the timer, returning the info structure
	time_t (*ticks)(timekeeper_source_info_t *private); // returns a monotonic number of ticks
	int flags;
} timekeeper_source_t;

// initializes the highest priority source that can be used as an early timer and switches the cpu to use it
void timekeeper_early_init(time_t us_offset);

// initializes the highest priority source and switches the cpu to use it. uses the early timer for calibration if nescessary
void timekeeper_init(void);

// spins waiting for us to pass
void timekeeper_wait_us(time_t us);

void timekeeper_sync(void);

timespec_t timekeeper_timefromboot(void);
timespec_t timekeeper_time(void);

#define TIMEKEEPER_SOURCE(x, ...) \
	static timekeeper_source_t x = { \
		__VA_ARGS__ \
	}; \
	__attribute__((section(".timekeeper_sources"), used)) static timekeeper_source_t *timekeeper_source_##x = &x;

#endif
