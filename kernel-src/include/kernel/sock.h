#ifndef _SOCK_H
#define _SOCK_H

#include <kernel/net.h>
#include <kernel/vfs.h>
#include <mutex.h>
#include <kernel/poll.h>
#include <kernel/usercopy.h>
#include <kernel/iovec.h>

#define SOCKET_STATE_UNBOUND 0
#define SOCKET_STATE_BOUND 1
#define SOCKET_STATE_CONNECTED 2
#define SOCKET_STATE_LISTENING 3

typedef struct {
	struct socketops_t *ops;
	mutex_t mutex;
	int state;
	int broadcast;
	netdev_t *netdev;
	pollheader_t pollheader;
	int type;
} socket_t;

typedef struct {
	union {
		ipv4addr_t ipv4addr;
		char path[256];
	};
} sockaddr_t;

typedef struct {
	size_t length;
	int level;
	int type;
	uint8_t data[];
} sockctrl_t;

typedef struct {
	sockaddr_t *addr;
	iovec_iterator_t iovec_iterator;
	size_t count;
	uintmax_t flags;
	size_t donecount;
	sockctrl_t *ctrl;
	size_t ctrllen;
	size_t ctrldone;
} sockdesc_t;

#define SOCKET_RECV_FLAGS_PEEK 0x8000000000000000l
#define SOCKET_RECV_FLAGS_WAITALL 0x4000000000000000l
#define SOCKET_RECV_FLAGS_CTRLTRUNCATED 0x2000000000000000l
#define SOCKET_SEND_FLAGS_NOSIGNAL 0x8000000000000000l

typedef struct socketops_t {
	int (*bind)(socket_t *socket, sockaddr_t *addr, cred_t *cred);
	int (*send)(socket_t *socket, sockdesc_t *desc);
	int (*recv)(socket_t *socket, sockdesc_t *desc);
	int (*poll)(socket_t *socket, polldata_t *data, int events);
	int (*connect)(socket_t *socket, sockaddr_t *addr, uintmax_t data, cred_t *cred);
	int (*listen)(socket_t *socket, int backlog);
	int (*accept)(socket_t *server, socket_t *client, sockaddr_t *addr, uintmax_t flags);
	int (*getname)(socket_t *socket, sockaddr_t *addr);
	int (*getpeername)(socket_t *socket, sockaddr_t *addr);
	size_t (*datacount)(socket_t *socket);
	void (*destroy)(socket_t *socket);
	int (*setopt)(socket_t *socket, int optname, void *buffer, size_t len, cred_t *cred);
} socketops_t;

typedef struct {
	vnode_t vnode;
	vattr_t attr;
	socket_t *socket;
} socketnode_t;

#define SOCKET_TYPE_UDP 0
#define SOCKET_TYPE_LOCAL 1
#define SOCKET_TYPE_TCP 2
#define SOCKFS_SOCKET_FROM_NODE(nodep) (((socketnode_t *)(nodep))->socket)

static inline int socket_read(socket_t *socket, void *buffer, size_t size, uintmax_t flags, size_t *bytes_read) {
	sockdesc_t desc = {
		.addr = NULL,
		.count = size,
		.flags = flags,
		.donecount = 0,
		.ctrl = NULL,
		.ctrllen = 0
	};

	iovec_t iovec = {
		.addr = buffer,
		.len = size
	};

	iovec_iterator_init(&desc.iovec_iterator, &iovec, 1);

	int e = socket->ops->recv(socket, &desc);

	*bytes_read = desc.donecount;

	return e;
}

static inline int socket_write(socket_t *socket, void *buffer, size_t size, uintmax_t flags, size_t *bytes_written) {
	sockdesc_t desc = {
		.addr = NULL,
		.count = size,
		.flags = flags,
		.donecount = 0,
		.ctrl = NULL,
		.ctrllen = 0
	};

	iovec_t iovec = {
		.addr = buffer,
		.len = size
	};

	iovec_iterator_init(&desc.iovec_iterator, &iovec, 1);

	int e = socket->ops->send(socket, &desc);

	*bytes_written = desc.donecount;
	return e;
}

static inline int sock_convertaddress(sockaddr_t *sockaddr, abisockaddr_t *abisockaddr) {
	switch (abisockaddr->type) {
		case AF_INET:
			inaddr_t *inaddr = (inaddr_t *)abisockaddr;
			sockaddr->ipv4addr.addr = be_to_cpu_d(inaddr->sin_addr);
			sockaddr->ipv4addr.port = be_to_cpu_w(inaddr->sin_port);
			break;
		case AF_LOCAL:
			unaddr_t *unaddr = (unaddr_t *)abisockaddr;
			strcpy(sockaddr->path, unaddr->sun_path);
			break;
		default:
			return EINVAL;
	}

	return 0;
}

static inline int sock_addrtoabiaddr(int socktype, sockaddr_t *sockaddr, abisockaddr_t *abisockaddr) {
	switch (socktype) {
		case SOCKET_TYPE_UDP:
		case SOCKET_TYPE_TCP:
			abisockaddr->type = AF_INET;
			inaddr_t *inaddr = (inaddr_t *)abisockaddr;
			inaddr->sin_addr = cpu_to_be_d(sockaddr->ipv4addr.addr);
			inaddr->sin_port = cpu_to_be_w(sockaddr->ipv4addr.port);
			break;
		case SOCKET_TYPE_LOCAL:
			abisockaddr->type = AF_LOCAL;
			unaddr_t *unaddr = (unaddr_t *)abisockaddr;
			strcpy(unaddr->sun_path, sockaddr->path);
			break;
		default:
			return EINVAL;
	}

	return 0;
}

static inline int sock_copymsghdr(msghdr_t *khdr, msghdr_t *uhdr) {
	if (usercopy_fromuser(khdr, uhdr, sizeof(msghdr_t)))
		return EFAULT;

	iovec_t *iovectmp = alloc(sizeof(iovec_t) * khdr->iovcount);
	if (iovectmp == NULL)
		return ENOMEM;

	if (usercopy_fromuser(iovectmp, khdr->iov, sizeof(iovec_t) * khdr->iovcount))
		return EFAULT;

	khdr->iov = iovectmp;

	if (khdr->addr) {
		abisockaddr_t *addrtmp = alloc(khdr->addrlen);
		if (addrtmp == NULL) {
			free(iovectmp);
			return ENOMEM;
		}

		if (usercopy_fromuser(addrtmp, khdr->addr, khdr->addrlen)) {
			free(iovectmp);
			return EFAULT;
		}

		khdr->addr = addrtmp;
	}

	if (khdr->msgctrl) {
		void *ctrltmp = alloc(khdr->ctrllen);
		if (ctrltmp == NULL) {
			if (khdr->addr)
				free(khdr->addr);
			free(iovectmp);
			return ENOMEM;
		}

		if (usercopy_fromuser(ctrltmp, khdr->msgctrl, khdr->ctrllen)) {
			if (khdr->addr)
				free(khdr->addr);
			free(iovectmp);
			return EFAULT;
		}

		khdr->msgctrl = ctrltmp;
	}

	return 0;
}

static inline void sock_freemsghdr(msghdr_t *hdr) {
	free(hdr->iov);
	if (hdr->addr)
		free(hdr->addr);
	if (hdr->msgctrl)
		free(hdr->msgctrl);
}

#define SOCK_CTRL_NEXT(c) ((sockctrl_t *)((uintptr_t)(c) + (c)->length))

static inline size_t sock_countctrl(sockctrl_t *ctrl, size_t len) {
	size_t count = 0;
	uintmax_t offset = 0;

	while (offset < len) {
		if (ctrl->length < sizeof(sockctrl_t))
			break;

		offset += ctrl->length;

		if (offset > len || offset < ctrl->length)
			break;

		++count;
		ctrl = SOCK_CTRL_NEXT(ctrl);
	}

	return count;
}

void localsock_leavebinding(vnode_t *vnode);
int localsock_pair(socket_t **ret1, socket_t **ret2);
socket_t *localsock_createsocket();
socket_t *udp_createsocket();
socket_t *tcp_createsocket();
socket_t *socket_create(int type);
int sockfs_newsocket(vnode_t **vnodep, socket_t *socket);
void sockfs_init();

#endif
