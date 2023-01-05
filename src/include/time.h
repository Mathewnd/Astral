#ifndef _TIME_H_INCLUDE
#define _TIME_H_INCLUDE

#define NS_IN_SECS 1000000000
#define US_IN_SECS 1000000
#define MS_IN_SECS 1000

#define US_IN_MS 1000
#define NS_IN_MS 1000000

#define NS_IN_US 1000

#include <math.h>

typedef long time_t;

struct timespec {
        time_t tv_sec;
        long tv_nsec;
};

static inline time_t time_diffms(struct timespec a, struct timespec b){
	time_t nsecdiff = labs(a.tv_nsec - a.tv_nsec);
	time_t secdiff = labs(a.tv_sec - a.tv_sec);	

	return nsecdiff / NS_IN_MS + secdiff * MS_IN_SECS;

}

#endif
