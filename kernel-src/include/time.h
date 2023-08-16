#ifndef _TIME_H
#define _TIME_H

#include <util.h>

typedef long time_t;

typedef struct {
	time_t s;
	time_t ns;
} timespec_t;

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

#endif
