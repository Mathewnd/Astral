#ifndef _POLL_H
#define _POLL_H

#include <spinlock.h>
#include <stddef.h>

struct polldata;

typedef struct {
	struct thread_t *thread;
	spinlock_t lock;
	spinlock_t eventlock;
	spinlock_t wakeuplock;
	struct polldata *data;
	struct polldata *event;
	size_t size;
} polldesc_t;

typedef struct {
	spinlock_t lock;
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
	SPINLOCK_INIT((x)->lock);

int poll_initdesc(polldesc_t *, size_t size);
void poll_add(pollheader_t *, polldata_t *, int events);
void poll_leave(polldesc_t *);
int poll_dowait(polldesc_t *, size_t ustimeout);
void poll_event(pollheader_t *, int events);
void poll_destroydesc(polldesc_t *);

#endif
