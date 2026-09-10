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
	uint16_t proto;
} packet_header_t;

typedef struct {
	socket_t socket;
	spinlock_t ringbuffer_lock;
	ringbuffer_t ringbuffer;
	list_node_t list_node;
	netdev_t *netdev_binding;
	uint16_t netdev_index;
	uint16_t proto;
} raw_socket_t;

// expectd to be called at IPL_DPC
void raw_process(netdev_t *netdev, void *buffer, mac_t *sender, uint16_t proto, size_t size) {
	spinlock_acquire(&list_lock);

	list_for_each (&socket_list, node) {
		raw_socket_t *socket = container_of(node, raw_socket_t, list_node);

		spinlock_acquire(&socket->ringbuffer_lock);
		if ((socket->netdev_binding && socket->netdev_binding != netdev) ||
			(socket->proto && socket->proto != proto) ||
			(RINGBUFFER_FREESPACE(&socket->ringbuffer) < size + sizeof(packet_header_t))) {
			spinlock_release(&socket->ringbuffer_lock);
			continue;
		}

		packet_header_t header;
		memcpy(header.sender, sender, 6);
		header.size = size;
		header.proto = proto;

		ringbuffer_write(&socket->ringbuffer, &header, sizeof(header));
		ringbuffer_write(&socket->ringbuffer, buffer, size);
		poll_event(&socket->socket.pollheader, POLLIN);
		spinlock_release(&socket->ringbuffer_lock);
	}

	spinlock_release(&list_lock);
}

static int raw_bind(socket_t *socket_handle, sockaddr_t *addr_handle, cred_t *cred) {
	raw_socket_t *socket = (raw_socket_t *)socket_handle;
	raw_addr_t *addr = &addr_handle->raw;

	if (addr->netdev == 0)
		return EINVAL;

	netdev_t *netdev = netdev_from_minor(addr->netdev - 1);
	if (netdev == NULL)
		return EINVAL;

	long ipl = spinlock_acquire_raise_ipl(&socket->ringbuffer_lock, IPL_DPC);
	socket->netdev_index = addr->netdev;
	socket->proto = addr->proto;
	socket->netdev_binding = netdev;
	spinlock_release_lower_ipl(&socket->ringbuffer_lock, ipl);
	return 0;
}

static int internalpoll(raw_socket_t *socket, polldata_t *data, int events) {
	int revents = 0;

	if (events & POLLOUT)
		revents |= POLLOUT;

	if ((events & POLLIN) && RINGBUFFER_DATACOUNT(&socket->ringbuffer))
		revents |= POLLIN;

	if (revents == 0 && data)
		poll_add(&socket->socket.pollheader, data, events);

	return revents;
}

static int raw_send(socket_t *socket_handle, sockdesc_t *sockdesc) {
	raw_socket_t *socket = (raw_socket_t *)socket_handle;
	int error = 0;
	netdesc_t desc;

	sockdesc->donecount = 0;
	MUTEX_ACQUIRE(&socket->socket.mutex);

	if (socket->socket.shutdown & SOCKET_SHUTDOWN_WRITE)
		goto leave;

	if (sockdesc->addr == NULL) {
		error = EDESTADDRREQ;
		goto leave;
	}

	if (socket->netdev_binding == NULL) {
		error = ENOTCONN;
		goto leave;
	}

	if (sockdesc->count > socket->netdev_binding->mtu) {
		error = EMSGSIZE;
		goto leave;
	}

	error = socket->netdev_binding->allocdesc(socket->netdev_binding, sockdesc->count, &desc);
	if (error)
		goto leave;

	error = iovec_iterator_copy_to_buffer(sockdesc->iovec_iterator, (void *)((uintptr_t)desc.address + desc.curroffset), sockdesc->count);
	if (error) {
		socket->netdev_binding->freedesc(socket->netdev_binding, &desc);
		goto leave;
	}

	mac_t target;
	memcpy(&target, sockdesc->addr->raw.mac, sizeof(target));
	error = socket->netdev_binding->sendpacket(socket->netdev_binding, desc, target, socket->proto);
	if (error == 0)
		sockdesc->donecount = sockdesc->count;

	leave:
	MUTEX_RELEASE(&socket->socket.mutex);
	return error;
}

static int raw_recv(socket_t *socket_handle, sockdesc_t *sockdesc) {
	raw_socket_t *socket = (raw_socket_t *)socket_handle;
	uintmax_t flags = sockdesc->flags;
	int error = 0;

	sockdesc->donecount = 0;
	MUTEX_ACQUIRE(&socket->socket.mutex);

	for (;;) {
		polldesc_t desc = {0};
		error = poll_initdesc(&desc, 1);
		if (error)
			goto leave;

		int revents = internalpoll(socket, &desc.data[0], POLLIN);
		if (revents) {
			poll_leave(&desc);
			poll_destroydesc(&desc);
			break;
		}

		if (socket_nonblocking(&socket->socket, flags)) {
			error = EAGAIN;
			poll_leave(&desc);
			poll_destroydesc(&desc);
			goto leave;
		}

		MUTEX_RELEASE(&socket->socket.mutex);

		error = poll_dowait(&desc, 0);

		poll_leave(&desc);
		poll_destroydesc(&desc);
		if (error)
			return error;

		MUTEX_ACQUIRE(&socket->socket.mutex);
	}

	long ipl = spinlock_acquire_raise_ipl(&socket->ringbuffer_lock, IPL_DPC);

	packet_header_t header;
	size_t header_size = ringbuffer_peek(&socket->ringbuffer, &header, 0, sizeof(header));

	spinlock_release_lower_ipl(&socket->ringbuffer_lock, ipl);

	__assert(header_size == sizeof(header));

	size_t copycount = min(header.size, sockdesc->count);
	sockdesc->donecount = iovec_iterator_peek_from_ringbuffer(sockdesc->iovec_iterator, &socket->ringbuffer, sizeof(header), copycount);
	if (sockdesc->donecount == RINGBUFFER_USER_COPY_FAILED) {
		sockdesc->donecount = 0;
		error = EFAULT;
		goto leave;
	}

	if ((flags & SOCKET_RECV_FLAGS_PEEK) == 0)
		ringbuffer_truncate(&socket->ringbuffer, sizeof(header) + header.size);

	if (sockdesc->addr) {
		memcpy(sockdesc->addr->raw.mac, header.sender, sizeof(sockdesc->addr->raw.mac));
		sockdesc->addr->raw.proto = header.proto;
	}

	leave:
	MUTEX_RELEASE(&socket->socket.mutex);
	return error;
}

static int raw_getname(socket_t *socket_handle, sockaddr_t *addr) {
	raw_socket_t *socket = (raw_socket_t *)socket_handle;

	long ipl = spinlock_acquire_raise_ipl(&socket->ringbuffer_lock, IPL_DPC);

	memset(addr, 0, sizeof(addr->raw));

	addr->raw.netdev = socket->netdev_index;
	addr->raw.proto = socket->proto;
	if (socket->netdev_binding)
		memcpy(addr->raw.mac, socket->netdev_binding->mac.address, sizeof(addr->raw.mac));

	spinlock_release_lower_ipl(&socket->ringbuffer_lock, ipl);

	return 0;
}

static int raw_poll(socket_t *handle, polldata_t *data, int events) {
	raw_socket_t *socket = (raw_socket_t *)handle;
	int revents = 0;
	MUTEX_ACQUIRE(&socket->socket.mutex);

	revents = internalpoll(socket, data, events);

	MUTEX_RELEASE(&socket->socket.mutex);
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

static int raw_getpeername(socket_t *socket, sockaddr_t *addr) {
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

socket_t *raw_create_socket(int protocol) {
	// XXX possibly move this to a slab?
	raw_socket_t *socket = alloc(sizeof(raw_socket_t));
	if (socket == NULL)
		return NULL;

	if (ringbuffer_init(&socket->ringbuffer, SOCKET_BUFFER)) {
		free(socket);
		return NULL;
	}

	SPINLOCK_INIT(socket->ringbuffer_lock);
	socket->proto = protocol;
	socket->socket.ops = &socketops;

	long ipl = spinlock_acquire_raise_ipl(&list_lock, IPL_DPC);
	list_push_front(&socket_list, &socket->list_node);
	spinlock_release_lower_ipl(&list_lock, ipl);

	return (socket_t *)socket;
}
