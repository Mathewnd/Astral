#include <kernel/sock.h>
#include <logging.h>

static socket_t *(*createsocket[])() = {
	udp_createsocket
};


socket_t *socket_create(int type) {
	__assert(type < sizeof(createsocket) / sizeof(createsocket[0]));
	return createsocket[type]();
}
