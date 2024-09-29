#ifndef _TIME_H
#define _TIME_H

#include <util.h>
#include <stdbool.h>

typedef long time_t;
typedef long suseconds_t;

typedef struct {
	time_t s;
	time_t ns;
} timespec_t;

typedef struct {
	time_t s;
	suseconds_t us;
} timeval_t;

static inline timespec_t timespec_add(timespec_t a, timespec_t b) {
	timespec_t ts;
	ts.ns = (a.ns + b.ns) % 1000000000;
	ts.s = a.s + b.s + (a.ns + b.ns) / 1000000000;
	return ts;
}

static inline time_t timespec_diffms(timespec_t a, timespec_t b) {
	time_t nsecdiff = abs(a.ns - b.ns);
	time_t secdiff = abs(a.s - b.s);
	return nsecdiff / 1000000 + secdiff * 1000;
}

static inline time_t timespec_diffus(timespec_t a, timespec_t b) {
	time_t nsecdiff = abs(a.ns - b.ns);
	time_t secdiff = abs(a.s - b.s);
	return nsecdiff / 1000 + secdiff * 1000000;
}

static inline time_t timespec_ns(timespec_t a) {
	return a.ns + a.s * 1000000000;
}

static inline time_t timespec_us(timespec_t a) {
	return a.ns / 1000 + a.s * 1000000;
}

static inline timespec_t timespec_from_us(time_t us) {
	timespec_t ts = {
		.s = us / 1000000,
		.ns = (us % 1000000) * 100
	};

	return ts;
}

static inline bool timespec_bigger(timespec_t t1, timespec_t t2) {
	return t1.s > t2.s || (t1.s == t2.s && t1.ns > t2.ns);
}

#endif
