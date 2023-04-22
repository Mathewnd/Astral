#ifndef _TIME_H
#define _TIME_H

typedef long time_t;

typedef struct {
	time_t s;
	time_t ns;
} timespec_t;

#endif
