#include <kernel/net.h>
#include <arch/cpu.h>
#include <logging.h>
#include <kernel/sock.h>
#include <kernel/alloc.h>
#include <spinlock.h>
#include <kernel/interrupt.h>
#include <ringbuffer.h>
#include <kernel/auth.h>
#include <list.h>

#define SOCKET_BUFFER (16 * 1024)

// TODO: check user creating socket is root

SPINLOCK_DEFINE(list_lock);
static list_t socket_list;

typedef struct {
	uint8_t sender[6];
	uint16_t size;
} packet_header_t;

typedef struct {
	socket_t socket;
	ringbuffer_t ringbuffer;
	list_node_t list_node;
	netdev_t *netdev_binding;
} raw_socket_t;

// expectd to be called at IPL_DPC
void raw_process(netdev_t *netdev, void *buffer, mac_t *sender, uint16_t proto, size_t size) {
	spinlock_acquire(&list_lock);

	list_for_each (&socket_list, node) {
		raw_socket_t *socket = container_of(node, raw_socket_t, list_node);
		if (socket->netdev_binding && socket->netdev_binding != netdev)
			continue;

		if (RINGBUFFER_FREESPACE(&socket->ringbuffer) < size + sizeof(packet_header_t))
			continue;

		packet_header_t header;
		memcpy(header.sender, sender, 6);
		header.size = size;

		ringbuffer_write(&socket->ringbuffer, &header, sizeof(header));
		ringbuffer_write(&socket->ringbuffer, buffer, size);
	}

	spinlock_release(&list_lock);
}

static int raw_bind(socket_t *socket, sockaddr_t *addr, cred_t *cred) {
	
}

static int internalpoll(socket_t *socket, polldata_t *data, int events) {

}

static int raw_send(socket_t *socket, sockdesc_t *sockdesc) {

}

static int raw_recv(socket_t *socket, sockdesc_t *sockdesc) {

}

static int raw_getname(socket_t *socket, sockaddr_t *addr) {
	raw_socket_t *raw_socket = (raw_socket_t *)socket;
	return 0;
}

static int raw_getpeername(socket_t *socket, sockaddr_t *addr) {
	return EINVAL;
}

static int raw_poll(socket_t *socket, polldata_t *data, int events) {
	int revents = 0;
	MUTEX_ACQUIRE(&socket->mutex);

	revents = internalpoll(socket, data, events);

	MUTEX_RELEASE(&socket->mutex);
	return revents;
}

static int raw_shutdown(socket_t *socket, int how) {
	if (how > SOCKET_SHUTDOWN_RW)
		return EINVAL;

	MUTEX_ACQUIRE(&socket->mutex);

	socket->shutdown |= how;

	MUTEX_RELEASE(&socket->mutex);
	return 0;
}

static int raw_connect(socket_t *socket, sockaddr_t *addr, uintmax_t flags, cred_t *cred) {
	return EINVAL;
}

static void raw_destroy(socket_t *handle) {
	raw_socket_t *socket = (raw_socket_t *)handle;
	long ipl = spinlock_acquire_raise_ipl(&list_lock, IPL_DPC);
	list_remove(&socket_list, &socket->list_node);
	spinlock_release_lower_ipl(&list_lock, ipl);
	free(socket);
}

static socketops_t socketops = {
	.bind = raw_bind,
	.send = raw_send,
	.recv = raw_recv,
	.destroy = raw_destroy,
	.poll = raw_poll,
	.getname = raw_getname,
	.getpeername = raw_getpeername,
	.shutdown = raw_shutdown,
	.connect = raw_connect
};

socket_t *raw_create_socket(void) {
	// XXX possibly move this to a slab?
	raw_socket_t *socket = alloc(sizeof(raw_socket_t));
	if (socket == NULL)
		return NULL;

	if (ringbuffer_init(&socket->ringbuffer, SOCKET_BUFFER)) {
		free(socket);
		return NULL;
	}

	socket->socket.ops = &socketops;

	long ipl = spinlock_acquire_raise_ipl(&list_lock, IPL_DPC);
	list_push_front(&socket_list, &socket->list_node);
	spinlock_release_lower_ipl(&list_lock, ipl);

	return (socket_t *)socket;
}
