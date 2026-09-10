#ifndef _SOCK_H
#define _SOCK_H

#include <kernel/net.h>
#include <kernel/vfs.h>
#include <kernel/file.h>
#include <mutex.h>
#include <kernel/poll.h>
#include <kernel/usercopy.h>
#include <kernel/iovec.h>
#include <stdbool.h>
#include <errno.h>
#include <kernel/raw.h>

#define SOCKET_STATE_UNBOUND 0
#define SOCKET_STATE_BOUND 1
#define SOCKET_STATE_CONNECTED 2
#define SOCKET_STATE_LISTENING 3

#define SOCKET_SHUTDOWN_READ 1
#define SOCKET_SHUTDOWN_WRITE 2
#define SOCKET_SHUTDOWN_RW 3

typedef struct socket {
	struct socketops_t *ops;
	mutex_t mutex;
	int state;
	int broadcast;
	netdev_t *netdev;
	pollheader_t pollheader;
	int type;
	int protocol;
	int shutdown;
	int error;
	bool nonblocking;
} socket_t;

typedef struct {
	union {
		ipv4addr_t ipv4addr;
		char path[256];
		raw_addr_t raw;
	};
} sockaddr_t;

typedef struct {
	socklen_t length;
	int padding;
	int level;
	int type;
	uint8_t data[];
} sockctrl_t;

typedef struct {
	sockaddr_t *addr;
	iovec_iterator_t *iovec_iterator;
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
#define SOCKET_RECV_FLAGS_CLOEXEC_CTRL 0x1000000000000000l
#define SOCKET_SEND_FLAGS_NOSIGNAL 0x8000000000000000l

typedef struct socketops_t {
	int (*bind)(socket_t *socket, sockaddr_t *addr, cred_t *cred);
	int (*send)(socket_t *socket, sockdesc_t *desc);
	int (*recv)(socket_t *socket, sockdesc_t *desc);
	int (*poll)(socket_t *socket, polldata_t *data, int events);
	int (*connect)(socket_t *socket, sockaddr_t *addr, uintmax_t flags, cred_t *cred);
	int (*listen)(socket_t *socket, int backlog);
	int (*accept)(socket_t *server, socket_t *client, sockaddr_t *addr, uintmax_t flags);
	int (*getname)(socket_t *socket, sockaddr_t *addr);
	int (*getpeername)(socket_t *socket, sockaddr_t *addr);
	size_t (*datacount)(socket_t *socket);
	void (*destroy)(socket_t *socket);
	int (*setopt)(socket_t *socket, int layer, int optname, void *buffer, socklen_t len, cred_t *cred);
	int (*getopt)(socket_t *socket, int layer, int optname, void *buffer, socklen_t *len, cred_t *cred);
	int (*shutdown)(socket_t *socket, int how);
} socketops_t;

typedef struct {
	vnode_t vnode;
	vattr_t attr;
	socket_t *socket;
} socketnode_t;

#define SOCKET_TYPE_UDP 0
#define SOCKET_TYPE_LOCAL 1
#define SOCKET_TYPE_TCP 2
#define SOCKET_TYPE_LOCAL_SEQPACKET 3
#define SOCKET_TYPE_RAW_STRIPPED 4
#define SOCKFS_SOCKET_FROM_NODE(nodep) (((socketnode_t *)(nodep))->socket)

static inline int sockfd_get(int fd, vnode_t **vnodep, int *fileflags) {
	file_t *file = fd_get(fd);
	if (file == NULL)
		return EBADF;

	if (file->vnode->type != V_TYPE_SOCKET) {
		fd_release(file);
		return ENOTSOCK;
	}

	vnode_t *vnode = file->vnode;
	VOP_HOLD(vnode);
	if (fileflags)
		*fileflags = file->flags;
	fd_release(file);

	*vnodep = vnode;
	return 0;
}

static inline bool socket_nonblocking(socket_t *socket, uintmax_t flags) {
	return socket->nonblocking || (flags & V_FFLAGS_NONBLOCKING);
}

static inline int socket_recv(socket_t *socket, void *buffer, size_t size, uintmax_t flags, size_t *bytes_read) {
	iovec_iterator_t iovec_iterator;

	sockdesc_t desc = {
		.addr = NULL,
		.iovec_iterator = &iovec_iterator,
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

	iovec_iterator_init(desc.iovec_iterator, &iovec, 1);

	int e = socket->ops->recv(socket, &desc);

	*bytes_read = desc.donecount;

	return e;
}

static inline int socket_send(socket_t *socket, void *buffer, size_t size, uintmax_t flags, size_t *bytes_written) {
	iovec_iterator_t iovec_iterator;

	sockdesc_t desc = {
		.addr = NULL,
		.iovec_iterator = &iovec_iterator,
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

	iovec_iterator_init(desc.iovec_iterator, &iovec, 1);

	int e = socket->ops->send(socket, &desc);

	*bytes_written = desc.donecount;
	return e;
}

static inline int sock_convertaddress(sockaddr_t *sockaddr, void *abisockaddr) {
	switch (((abisockaddr_t *)abisockaddr)->type) {
		case AF_INET:
			inaddr_t *inaddr = (inaddr_t *)abisockaddr;
			sockaddr->ipv4addr.addr = be_to_cpu_d(inaddr->sin_addr);
			sockaddr->ipv4addr.port = be_to_cpu_w(inaddr->sin_port);
			break;
		case AF_LOCAL:
			unaddr_t *unaddr = (unaddr_t *)abisockaddr;
			size_t length = strnlen(unaddr->sun_path, ABISOCKADDR_UN_MAX);
			if (length == ABISOCKADDR_UN_MAX)
				return EINVAL;

			memcpy(sockaddr->path, unaddr->sun_path, length + 1);
			break;
		case AF_PACKET: {
			sockaddr_ll_t lladdr;
			memcpy(&lladdr, abisockaddr, sizeof(lladdr));
			if (lladdr.sll_ifindex < 0 || lladdr.sll_ifindex > 0xffff)
				return EINVAL;

			sockaddr->raw.netdev = lladdr.sll_ifindex;
			sockaddr->raw.proto = be_to_cpu_w(lladdr.sll_protocol);
			memset(sockaddr->raw.mac, 0, sizeof(sockaddr->raw.mac));
			memcpy(sockaddr->raw.mac, lladdr.sll_addr, min((size_t)lladdr.sll_halen, sizeof(sockaddr->raw.mac)));
			break;
		}
		default:
			return EINVAL;
	}

	return 0;
}

static inline int sock_addrtoabiaddr(int socktype, sockaddr_t *sockaddr, void *abisockaddr) {
	switch (socktype) {
		case SOCKET_TYPE_UDP:
		case SOCKET_TYPE_TCP:
			((abisockaddr_t *)abisockaddr)->type = AF_INET;
			inaddr_t *inaddr = (inaddr_t *)abisockaddr;
			inaddr->sin_addr = cpu_to_be_d(sockaddr->ipv4addr.addr);
			inaddr->sin_port = cpu_to_be_w(sockaddr->ipv4addr.port);
			break;
		case SOCKET_TYPE_LOCAL:
		case SOCKET_TYPE_LOCAL_SEQPACKET:
			((abisockaddr_t *)abisockaddr)->type = AF_LOCAL;
			unaddr_t *unaddr = (unaddr_t *)abisockaddr;
			strcpy(unaddr->sun_path, sockaddr->path);
			break;
		case SOCKET_TYPE_RAW_STRIPPED: {
			sockaddr_ll_t lladdr = {
				.sll_family = AF_PACKET,
				.sll_protocol = cpu_to_be_w(sockaddr->raw.proto),
				.sll_ifindex = sockaddr->raw.netdev,
				.sll_hatype = ARPHRD_ETHER,
				.sll_halen = sizeof(sockaddr->raw.mac)
			};
			memcpy(lladdr.sll_addr, sockaddr->raw.mac, sizeof(sockaddr->raw.mac));
			memcpy(abisockaddr, &lladdr, sizeof(lladdr));
			break;
		}
		default:
			return EINVAL;
	}

	return 0;
}

static inline int sock_copy_addr_to_user(int socktype, sockaddr_t *sockaddr, void *uaddr, socklen_t uaddrlen, socklen_t *actual_len) {
	switch (socktype) {
		case SOCKET_TYPE_UDP:
		case SOCKET_TYPE_TCP: {
			abisockaddr_t abisockaddr = {0};
			abisockaddr.type = AF_INET;
			inaddr_t *inaddr = (inaddr_t *)&abisockaddr;
			inaddr->sin_addr = cpu_to_be_d(sockaddr->ipv4addr.addr);
			inaddr->sin_port = cpu_to_be_w(sockaddr->ipv4addr.port);

			*actual_len = sizeof(abisockaddr);

			return usercopy_touser(uaddr, &abisockaddr, min(uaddrlen, sizeof(abisockaddr)));
		}
		case SOCKET_TYPE_LOCAL:
		case SOCKET_TYPE_LOCAL_SEQPACKET: {
			size_t path_len = strnlen(sockaddr->path, ABISOCKADDR_UN_MAX - 1) + 1;
			if (path_len == ABISOCKADDR_UN_MAX)
				return EINVAL;

			*actual_len = offsetof(unaddr_t, sun_path) + (path_len != 1 ? path_len : 0);

			uint16_t family = AF_LOCAL;
			if (usercopy_touser(uaddr, &family, min(uaddrlen, sizeof(family))))
				return EFAULT;

			if (uaddrlen > offsetof(unaddr_t, sun_path) && path_len) {
				size_t size = min(uaddrlen - offsetof(unaddr_t, sun_path), path_len);
				if (usercopy_touser((void *)((uintptr_t)uaddr + offsetof(unaddr_t, sun_path)), sockaddr->path, size))
					return EFAULT;
			}

			return 0;
		}
		case SOCKET_TYPE_RAW_STRIPPED: {
			sockaddr_ll_t lladdr = {0};
			sock_addrtoabiaddr(socktype, sockaddr, &lladdr);

			*actual_len = sizeof(lladdr);
			return usercopy_touser(uaddr, &lladdr, min(uaddrlen, sizeof(lladdr)));
		}
		default:
			return EINVAL;
	}
}

static inline int sock_copymsghdr(msghdr_t *khdr, msghdr_t *uhdr) {
	if (usercopy_fromuser(khdr, uhdr, sizeof(msghdr_t)))
		return EFAULT;

	iovec_t *iovectmp = NULL;
	if (khdr->iovcount) {
		iovectmp = alloc(sizeof(iovec_t) * khdr->iovcount);
		if (iovectmp == NULL)
			return ENOMEM;

		if (usercopy_fromuser(iovectmp, khdr->iov, sizeof(iovec_t) * khdr->iovcount)) {
			free(iovectmp);
			return EFAULT;
		}
	}

	khdr->iov = iovectmp;

	if (khdr->addr) {
		abisockaddr_t *addrtmp = alloc(khdr->addrlen);
		if (addrtmp == NULL) {
			if (iovectmp)
				free(iovectmp);
			return ENOMEM;
		}

		if (usercopy_fromuser(addrtmp, khdr->addr, khdr->addrlen)) {
			if (iovectmp)
				free(iovectmp);
			free(addrtmp);
			return EFAULT;
		}

		khdr->addr = addrtmp;
	}

	if (khdr->msgctrl && khdr->ctrllen) {
		void *ctrltmp = alloc(khdr->ctrllen);
		if (ctrltmp == NULL) {
			if (khdr->addr)
				free(khdr->addr);
			if (iovectmp)
				free(iovectmp);
			return ENOMEM;
		}

		if (usercopy_fromuser(ctrltmp, khdr->msgctrl, khdr->ctrllen)) {
			if (khdr->addr)
				free(khdr->addr);
			if (iovectmp)
				free(iovectmp);
			free(ctrltmp);
			return EFAULT;
		}

		khdr->msgctrl = ctrltmp;
	} else {
		khdr->msgctrl = NULL;
		khdr->ctrllen = 0;
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

#define SOCK_CTRL_ALIGN(x) (((x) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define SOCK_CTRL_LEN(x) (SOCK_CTRL_ALIGN(sizeof(sockctrl_t)) + (x))
#define SOCK_CTRL_SPACE(x) (SOCK_CTRL_ALIGN(sizeof(sockctrl_t)) + SOCK_CTRL_ALIGN(x))
#define SOCK_CTRL_DATALEN(c) ((c)->length - SOCK_CTRL_ALIGN(sizeof(sockctrl_t)))
#define SOCK_CTRL_NEXT(c) ((sockctrl_t *)((uintptr_t)(c) + SOCK_CTRL_ALIGN((c)->length)))

static inline size_t sock_countctrl(sockctrl_t *ctrl, size_t len) {
	size_t count = 0;
	size_t offset = 0;

	while (offset < len) {
		size_t remaining = len - offset;

		if (remaining < SOCK_CTRL_LEN(0))
			break;

		if (ctrl->length < SOCK_CTRL_LEN(0))
			break;

		if (ctrl->length > remaining)
			break;

		++count;

		size_t alignedlen = SOCK_CTRL_ALIGN(ctrl->length);
		if (alignedlen > remaining || alignedlen < ctrl->length)
			break;

		offset += alignedlen;
		ctrl = SOCK_CTRL_NEXT(ctrl);
	}

	return count;
}

void localsock_leavebinding(vnode_t *vnode);
int localsock_pair(socket_t **ret1, socket_t **ret2, int protocol);
int localsock_pair_seqpacket(socket_t **ret1, socket_t **ret2, int protocol);
socket_t *localsock_createsocket(int protocol);
socket_t *localsock_create_seqpacket_socket(int protocol);
socket_t *udp_createsocket(int protocol);
socket_t *tcp_createsocket(int protocol);
socket_t *socket_create(int type, int protocol);
int sockfs_newsocket(vnode_t **vnodep, socket_t *socket);
void sockfs_init();

#endif
