#ifndef _UNSOCKET_H_INCLUDE
#define _UNSOCKET_H_INCLUDE

#include <kernel/socket.h>

typedef struct {
	short sun_family;
	char sun_path[108];
} sockaddr_un;

struct _socket_t;
typedef unsigned socklen_t;

int unsocket_new(struct _socket_t** returnptr, int type, int protocol);
int unsocket_bind(struct _socket_t* sock, sockaddr_un* addr, socklen_t addrlen);
int unsocket_send(struct _socket_t* socket, void* buff, socklen_t len, int flags, int* error);
#endif
