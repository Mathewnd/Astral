#ifndef _UNSOCKET_H_INCLUDE
#define _UNSOCKET_H_INCLUDE

#include <kernel/socket.h>

typedef struct {
	short sun_family;
	char sun_path[108];
} sockaddr_un;

struct _socket_t;

int unsocket_new(struct _socket_t** returnptr, int type, int protocol);
int unsocket_bind(struct _socket_t* sock, sockaddr_un* addr, size_t addrlen);
int unsocket_send(struct _socket_t* socket, void* buff, size_t len, int flags, int* error);
#endif
