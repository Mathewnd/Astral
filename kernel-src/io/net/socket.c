#include <kernel/sock.h>
#include <logging.h>

static socket_t *(*createsocket[])() = {
	udp_createsocket,
	localsock_createsocket
};


socket_t *socket_create(int type) {
	__assert(type < sizeof(createsocket) / sizeof(createsocket[0]));
	socket_t *socket = createsocket[type]();
	if (socket == NULL)
		return NULL;

	POLL_INITHEADER(&socket->pollheader);
	socket->state = SOCKET_STATE_UNBOUND;
	MUTEX_INIT(&socket->mutex);
	socket->type = type;

	return socket;
}
