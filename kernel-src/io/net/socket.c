#include <kernel/sock.h>
#include <kernel/raw.h>
#include <logging.h>

static socket_t *(*createsocket[])(int) = {
	udp_createsocket,
	localsock_createsocket,
	tcp_createsocket,
	localsock_create_seqpacket_socket,
	raw_create_socket
};

socket_t *socket_create(int type, int protocol) {
	__assert(type < sizeof(createsocket) / sizeof(createsocket[0]));
	socket_t *socket = createsocket[type](protocol);
	if (socket == NULL)
		return NULL;

	POLL_INITHEADER(&socket->pollheader);
	socket->state = SOCKET_STATE_UNBOUND;
	MUTEX_INIT(&socket->mutex);
	socket->type = type;
	socket->protocol = protocol;
	socket->nonblocking = false;

	return socket;
}
