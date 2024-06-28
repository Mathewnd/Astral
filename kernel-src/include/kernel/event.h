#ifndef _EVENT_H
#define _EVENT_H

#include <kernel/poll.h>

typedef pollheader_t eventheader_t;

typedef struct {
	polldesc_t polldesc;
	polldata_t polldata;
} eventlistener_t;

#define EVENT_INITHEADER(e) POLL_INITHEADER(e)
#define EVENT_INITLISTENER(e) { \
		poll_initdesc(&(e)->polldesc, 0); \
		(e)->polldesc.data = &(e)->polldata; \
		(e)->polldata.desc = &(e)->polldesc; \
	}

#define EVENT_ATTACH(e, h) poll_add(h, &(e)->polldata, POLLPRI)
#define EVENT_WAIT(e, t) poll_dowait(&(e)->polldesc, t)
#define EVENT_SIGNAL(h) poll_event(h, POLLPRI)
#define EVENT_DETACHALL(e) poll_leave(&(e)->polldesc)

#endif
