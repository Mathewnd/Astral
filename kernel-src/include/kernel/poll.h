#ifndef _POLL_H
#define _POLL_H

#include <kernel/scheduler.h>
#include <spinlock.h>
#include <mutex.h>

struct polldata;

typedef struct {
	thread_t *thread;
	spinlock_t lock;
	mutex_t eventlock;
	struct polldata *data;
	struct polldata *event;
	size_t size;
} polldesc_t;

typedef struct {
	mutex_t lock;
	struct polldata *data;
} pollheader_t;

typedef struct polldata {
	struct polldata *next;
	struct polldata *prev;
	pollheader_t *header;
	polldesc_t *desc;
	int events;
	int revents;
} polldata_t;

#define POLL_INITHEADER(x) \
	MUTEX_INIT(&(x)->lock);

int poll_initdesc(polldesc_t *, size_t size);
void poll_add(pollheader_t *, polldata_t *, int events);
void poll_leave(polldesc_t *);
int poll_dowait(polldesc_t *);
void poll_event(pollheader_t *, int events);
void poll_destroydesc(polldesc_t *);

#endif
