#ifndef _SOCK_H
#define _SOCK_H

#include <kernel/net.h>
#include <kernel/vfs.h>
#include <mutex.h>
#include <kernel/poll.h>

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

typedef struct socketops_t {
	int (*bind)(socket_t *socket, sockaddr_t *addr);
	int (*send)(socket_t *socket, sockaddr_t *addr, void *buffer, size_t count, uintmax_t flags, size_t *sendcount);
	int (*recv)(socket_t *socket, sockaddr_t *addr, void *buffer, size_t count, uintmax_t flags, size_t *recvcount);
	int (*poll)(socket_t *socket, polldata_t *data, int events);
	void (*destroy)(socket_t *socket);
} socketops_t;

typedef struct {
	vnode_t vnode;
	vattr_t attr;
	socket_t *socket;
} socketnode_t;

#define SOCKET_TYPE_UDP 0
#define SOCKFS_SOCKET_FROM_NODE(nodep) (((socketnode_t *)(nodep))->socket)

static inline int sock_convertaddress(sockaddr_t *sockaddr, abisockaddr_t *abisockaddr) {
	switch (abisockaddr->type) {
		case AF_INET:
			inaddr_t *inaddr = (inaddr_t *)abisockaddr;
			sockaddr->ipv4addr.addr = be_to_cpu_d(inaddr->sin_addr);
			sockaddr->ipv4addr.port = be_to_cpu_w(inaddr->sin_port);
			break;
		default:
			return EINVAL;
	}

	return 0;
}

static inline int sock_addrtoabiaddr(int socktype, sockaddr_t *sockaddr, abisockaddr_t *abisockaddr) {
	switch (socktype) {
		case SOCKET_TYPE_UDP:
			inaddr_t *inaddr = (inaddr_t *)abisockaddr;
			inaddr->sin_addr = cpu_to_be_d(sockaddr->ipv4addr.addr);
			inaddr->sin_port = cpu_to_be_w(sockaddr->ipv4addr.port);
			break;
		default:
			return EINVAL;
	}

	return 0;
}

static inline int sock_copymsghdr(msghdr_t *khdr, msghdr_t *uhdr) {
	memcpy(khdr, uhdr, sizeof(msghdr_t));
	iovec_t *iovectmp = alloc(sizeof(iovec_t) * khdr->iovcount);
	if (iovectmp == NULL)
		return ENOMEM;
	memcpy(iovectmp, khdr->iov, sizeof(iovec_t) * khdr->iovcount);
	khdr->iov = iovectmp;

	if (khdr->addr) {
		abisockaddr_t *addrtmp = alloc(khdr->addrlen);
		if (addrtmp == NULL) {
			free(iovectmp);
			return ENOMEM;
		}
		memcpy(addrtmp, khdr->addr, khdr->addrlen);
		khdr->addr = addrtmp;
	}

	return 0;
}

static inline void sock_freemsghdr(msghdr_t *hdr) {
	free(hdr->iov);
	free(hdr->addr);
}

socket_t *udp_createsocket();
socket_t *socket_create(int type);
int sockfs_newsocket(vnode_t **vnodep, socket_t *socket);
void sockfs_init();

#endif
