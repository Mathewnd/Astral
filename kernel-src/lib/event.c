#include <event.h>

// TODO while this works, it is built upon semaphores and might have some race conditions

int event_wait(event_t *event, bool interruptible) {
	int e = semaphore_wait(event, interruptible);
	if (e)
		return e;

	if (semaphore_haswaiters(event))
		semaphore_signal(event);

	return 0;
}

void event_signal(event_t *event) {
	if (semaphore_haswaiters(event))
		semaphore_signal(event);
}
