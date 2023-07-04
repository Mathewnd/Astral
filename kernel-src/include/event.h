#ifndef _EVENT_H
#define _EVENT_H

#include <semaphore.h>
typedef semaphore_t event_t;

int event_wait(event_t *event, bool interruptible);
void event_signal(event_t *event);

#define EVENT_INIT(x) SEMAPHORE_INIT(x, 0);

#endif
