#ifndef _EVENT_H
#define _EVENT_H

#include <kernel/poll.h>

typedef pollheader_t eventheader_t;
#define EVENT_MAX_ATTACHMENT 5

typedef struct {
	polldesc_t polldesc;
	polldata_t polldata[EVENT_MAX_ATTACHMENT];
	int current;
} eventlistener_t;

#define EVENT_INITHEADER(e) POLL_INITHEADER(e)
#define EVENT_INITLISTENER(e) { \
		poll_initdesc(&(e)->polldesc, 0); \
		(e)->polldesc.data = (e)->polldata; \
		(e)->current = 0; \
		for (int i = 0; i < EVENT_MAX_ATTACHMENT; ++i) \
			(e)->polldata[i].desc = &(e)->polldesc; \
	}

#define EVENT_ATTACH(e, h) { \
		poll_add(h, &(e)->polldata[(e)->current++], POLLPRI); \
		if ((e)->current >= EVENT_MAX_ATTACHMENT) { \
			/* spin forever to say that there have been too many attachments */ \
			/* this should never happen normally and is meant as a way to alert whoever is programming about it */ \
			for (;;) asm volatile(""); \
		} \
	}

#define EVENT_WAIT(e, t) poll_dowait(&(e)->polldesc, t)
#define EVENT_SIGNAL(h) poll_event(h, POLLPRI)

#define EVENT_DETACHALL(e) { \
		poll_leave(&(e)->polldesc); \
		(e)->current = 0; \
	}

#endif
