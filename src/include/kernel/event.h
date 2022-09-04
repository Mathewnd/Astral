#ifndef _EVENT_H_INCLUDE
#define _EVENT_H_INCLUDE

#include <stdbool.h>
#include <stddef.h>

#define EVENT_MAX_THREADS 100

typedef struct _thread_t thread_t;

typedef struct _event_t{
	int lock;
	thread_t* threads[EVENT_MAX_THREADS];
} event_t;

#include <kernel/sched.h>

int event_signal(event_t* event, bool interruptson);
int event_wait(event_t* event, bool interruptible);

#endif
