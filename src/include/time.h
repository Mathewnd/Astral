#ifndef _TIME_H_INCLUDE
#define _TIME_H_INCLUDE

#define NS_IN_SECS 1000000000
#define US_IN_SECS 1000000
#define MS_IN_SECS 1000

#define US_IN_MS 1000
#define NS_IN_MS 1000000

#define NS_IN_US 1000

typedef long time_t;

struct timespec {
        time_t tv_sec;
        long tv_nsec;
};


#endif
