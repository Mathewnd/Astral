#ifndef _TIME_H
#define _TIME_H

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

#endif
