#include <kernel/net.h>
#include <arch/cpu.h>
#include <logging.h>
#include <kernel/sock.h>
#include <kernel/alloc.h>
#include <spinlock.h>
#include <kernel/interrupt.h>
#include <ringbuffer.h>
#include <kernel/auth.h>

#define SOCKET_BUFFER (256 * 1024)

typedef struct {
	uint32_t peer;
	uint16_t srcport;
	uint16_t length;
} dataheader_t;

typedef struct {
	socket_t socket;
	uint32_t address;
	uint16_t port;
	uint32_t peeraddress;
	uint32_t peerport;
	ringbuffer_t ringbuffer;
	spinlock_t ringbufferlock;
	int packetsprocessing;
} udpsocket_t;

#define ALLOC_RANGE_START 50000
#define ALLOC_RANGE_END   60000
static socket_t *ports[65536];
static spinlock_t portlock;

int udp_sendpacket(void *buffer, size_t size, uint32_t ip, uint16_t srcport, uint16_t dstport, netdev_t *broadcastnetdev) {
	void *newbuff = alloc(size + sizeof(udpframe_t));
	if (newbuff == NULL)
		return ENOMEM;

	udpframe_t frame = {
		.srcport = cpu_to_be_w(srcport),
		.dstport = cpu_to_be_w(dstport),
		.length = cpu_to_be_w(size + sizeof(udpframe_t)),
		.checksum = 0
	};

	memcpy(newbuff, &frame, sizeof(udpframe_t));
	memcpy((void *)((uintptr_t)newbuff + sizeof(udpframe_t)), buffer, size);

	int e = ipv4_sendpacket(newbuff, size + sizeof(udpframe_t), ip, IPV4_PROTO_UDP, broadcastnetdev);
	free(newbuff);
	return e;
}

void udp_init() {
	SPINLOCK_INIT(portlock);
}

int udp_allocport(socket_t *socket, uint16_t *port) {
	bool intstate = interrupt_set(false);
	spinlock_acquire(&portlock);
	if (*port == 0) {
		for (*port = ALLOC_RANGE_START; *port < ALLOC_RANGE_END; ++(*port))
			if (ports[*port] == NULL)
				break;

		__assert(*port != ALLOC_RANGE_END);
	}

	int e = 0;
	if (ports[*port] != NULL) {
		e = EADDRINUSE;
		goto cleanup;
	}

	ports[*port] = socket;

	cleanup:
	spinlock_release(&portlock);
	interrupt_set(intstate);

	return e;
}

void udp_unallocport(uint16_t port) {
	__assert(port > 0);
	bool intstate = interrupt_set(false);
	spinlock_acquire(&portlock);
	ports[port] = NULL;
	spinlock_release(&portlock);
	interrupt_set(intstate);
}

void udp_process(netdev_t *netdev, void *buffer, uint32_t peer) {
	udpframe_t *frame = buffer;
	uint16_t srcport = be_to_cpu_w(frame->srcport);
	uint16_t dstport = be_to_cpu_w(frame->dstport);
	uint16_t length = be_to_cpu_w(frame->length);

	spinlock_acquire(&portlock);

	udpsocket_t *socket = (udpsocket_t *)ports[dstport];
	if (socket)
		__atomic_add_fetch(&socket->packetsprocessing, 1, __ATOMIC_SEQ_CST);

	spinlock_release(&portlock);

	if (socket == NULL) // drop packet if no socket listening on port
		return;

	dataheader_t header = {
		.peer = peer,
		.srcport = srcport,
		.length = length - sizeof(udpframe_t)
	};

	void *writebuffer = (void *)((uintptr_t)buffer + sizeof(udpframe_t));
	size_t needed = header.length + sizeof(dataheader_t); 
	spinlock_acquire(&socket->ringbufferlock);
	size_t freespace = SOCKET_BUFFER - RINGBUFFER_DATACOUNT(&socket->ringbuffer);
	if (freespace >= needed) {
		__assert(ringbuffer_write(&socket->ringbuffer, &header, sizeof(header)) == sizeof(header));
		__assert(ringbuffer_write(&socket->ringbuffer, writebuffer, header.length) == header.length);
		poll_event(&socket->socket.pollheader, POLLIN);
	}
	spinlock_release(&socket->ringbufferlock);
	__atomic_sub_fetch(&socket->packetsprocessing, 1, __ATOMIC_SEQ_CST);
}

static int udp_bind(socket_t *socket, sockaddr_t *addr, cred_t *cred) {
	if (addr->ipv4addr.port != 0 && addr->ipv4addr.port < 1024) {
		// this port is reserved for privileged users
		int error = auth_network_check(cred, AUTH_ACTIONS_NETWORK_BINDRESERVED, socket, NULL);
		if (error)
			return error;
	}

	int e;
	MUTEX_ACQUIRE(&socket->mutex, false);
	if (socket->state != SOCKET_STATE_UNBOUND) {
		e = EINVAL;
		goto cleanup;
	}

	e = udp_allocport(socket, &addr->ipv4addr.port);

	if (e)
		goto cleanup;

	udpsocket_t *udpsocket = (udpsocket_t *)socket;
	udpsocket->port = addr->ipv4addr.port;
	udpsocket->address = addr->ipv4addr.addr;

	socket->state = SOCKET_STATE_BOUND;

	cleanup:
	MUTEX_RELEASE(&socket->mutex);
	return e;
}

static int internalpoll(socket_t *socket, polldata_t *data, int events) {
	udpsocket_t *udpsocket = (udpsocket_t *)socket;
	int revents = 0;

	if (events & POLLIN) {
		bool intstate = interrupt_set(false);
		spinlock_acquire(&udpsocket->ringbufferlock);
		if (RINGBUFFER_DATACOUNT(&udpsocket->ringbuffer) > 0)
			revents |= POLLIN;
		spinlock_release(&udpsocket->ringbufferlock);
		interrupt_set(intstate);
	}

	if (events & POLLOUT)
		revents |= POLLOUT;

	if (revents == 0 && data)
		poll_add(&socket->pollheader, data, events);

	return revents;
}

static int udp_send(socket_t *socket, sockaddr_t *addr, void *buffer, size_t count, uintmax_t flags, size_t *sendcount) {
	udpsocket_t *udpsocket = (udpsocket_t *)socket;
	int e;
	MUTEX_ACQUIRE(&socket->mutex, false);

	if (addr == NULL && socket->state != SOCKET_STATE_CONNECTED) {
		e = ENOTCONN;
		goto cleanup;
	}

	if ((addr ? addr->ipv4addr.addr : udpsocket->peeraddress) == IPV4_BROADCAST_ADDRESS && (udpsocket->socket.broadcast == 0 || udpsocket->socket.netdev == NULL)) {
		e = EACCES;
		goto cleanup;
	}

	if (socket->state == SOCKET_STATE_UNBOUND) {
		uint16_t port = 0;
		e = udp_allocport(socket, &port);
		udpsocket->port = port;
		udpsocket->address = 0;
		socket->state = SOCKET_STATE_BOUND;
	}

	e = udp_sendpacket(buffer, count, addr ? addr->ipv4addr.addr : udpsocket->peeraddress, udpsocket->port, addr ? addr->ipv4addr.port : udpsocket->peerport, udpsocket->socket.netdev);

	*sendcount = count;
	cleanup:
	MUTEX_RELEASE(&socket->mutex);
	return e;
}

static int udp_recv(socket_t *socket, sockaddr_t *addr, void *buffer, size_t count, uintmax_t flags, size_t *recvcount) {
	udpsocket_t *udpsocket = (udpsocket_t *)socket;
	int e = 0;
	MUTEX_ACQUIRE(&socket->mutex, false);

	if (udpsocket->port == 0) {
		e = EINVAL;
		goto cleanup;
	}

	for (;;) {
		polldesc_t desc = {0};
		e = poll_initdesc(&desc, 1);
		if (e)
			goto cleanup;

		int revents = internalpoll(socket, &desc.data[0], POLLIN);

		if (revents) {
			poll_leave(&desc);
			poll_destroydesc(&desc);
			break;
		}

		if (flags & V_FFLAGS_NONBLOCKING) {
			e = EAGAIN;
			poll_leave(&desc);
			poll_destroydesc(&desc);
			goto cleanup;
		}

		MUTEX_RELEASE(&socket->mutex);

		e = poll_dowait(&desc, 0);

		poll_leave(&desc);
		poll_destroydesc(&desc);

		if (e)
			return e;

		MUTEX_ACQUIRE(&socket->mutex, false);
	}

	bool intstate = interrupt_set(false);
	spinlock_acquire(&udpsocket->ringbufferlock);

	size_t copycount;
	dataheader_t header;
	if (flags & SOCKET_RECV_FLAGS_PEEK) {
		__assert(ringbuffer_peek(&udpsocket->ringbuffer, &header, 0, sizeof(header)) == sizeof(header));
		copycount = min(header.length, count);

		__assert(ringbuffer_peek(&udpsocket->ringbuffer, &buffer, sizeof(header), copycount) == copycount);
	} else {
		__assert(ringbuffer_read(&udpsocket->ringbuffer, &header, sizeof(header)) == sizeof(header));

		copycount = min(header.length, count);
		__assert(ringbuffer_read(&udpsocket->ringbuffer, buffer, copycount) == copycount);

		// truncate the rest if the buffer wasn't big enough to handle everything
		if (header.length > count)
			ringbuffer_truncate(&udpsocket->ringbuffer, header.length - copycount);
	}
	spinlock_release(&udpsocket->ringbufferlock);
	interrupt_set(intstate);

	*recvcount = copycount;
	addr->ipv4addr.addr = header.peer;
	addr->ipv4addr.port = header.srcport;

	cleanup:
	MUTEX_RELEASE(&socket->mutex);
	return e;
}

static void udp_destroy(socket_t *socket) {
	// we need to be aware that a packet can still arrive while we are destroying the socket
	udpsocket_t *udpsocket = (udpsocket_t *)socket;
	if (udpsocket->port)
		udp_unallocport(udpsocket->port);
	// from now on no new packet will arrive, but another cpu could be processing a packet for this socket
	// wait for all inbound packets to be processed
	while (__atomic_load_n(&udpsocket->packetsprocessing, __ATOMIC_SEQ_CST) > 0) CPU_PAUSE();

	// no risk of the socket object being used by udp_process() now
	ringbuffer_destroy(&udpsocket->ringbuffer);
	free(socket);
}

static int udp_getname(socket_t *socket, sockaddr_t *addr) {
	udpsocket_t *udpsocket = (udpsocket_t *)socket;
	MUTEX_ACQUIRE(&socket->mutex, false);
	addr->ipv4addr.addr = udpsocket->address;
	addr->ipv4addr.port = udpsocket->port;
	MUTEX_RELEASE(&socket->mutex);
	return 0;
}

static int udp_getpeername(socket_t *socket, sockaddr_t *addr) {
	udpsocket_t *udpsocket = (udpsocket_t *)socket;
	MUTEX_ACQUIRE(&socket->mutex, false);
	addr->ipv4addr.addr = udpsocket->peeraddress;
	addr->ipv4addr.port = udpsocket->peerport;
	MUTEX_RELEASE(&socket->mutex);
	return 0;
}

static int udp_poll(socket_t *socket, polldata_t *data, int events) {
	int revents = 0;
	MUTEX_ACQUIRE(&socket->mutex, false);

	revents = internalpoll(socket, data, events);

	MUTEX_RELEASE(&socket->mutex);
	return revents;
}

static socketops_t socketops = {
	.bind = udp_bind,
	.send = udp_send,
	.recv = udp_recv,
	.destroy = udp_destroy,
	.poll = udp_poll,
	.getname = udp_getname,
	.getpeername = udp_getpeername
};

socket_t *udp_createsocket() {
	// XXX possibly move this to a slab?
	udpsocket_t *socket = alloc(sizeof(udpsocket_t));
	if (socket == NULL)
		return NULL;

	if (ringbuffer_init(&socket->ringbuffer, SOCKET_BUFFER)) {
		free(socket);
		return NULL;
	}

	SPINLOCK_INIT(socket->ringbufferlock);
	socket->socket.ops = &socketops;

	return (socket_t *)socket;
}
