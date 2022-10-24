#ifndef _SOCKET_H_INCLUDE
#define _SOCKET_H_INCLUDE

#include <kernel/fd.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <kernel/event.h>
#include <kernel/unsocket.h>
#include <ringbuffer.h>

#define SOCKET_STATE_UNBOUND 0
#define SOCKET_STATE_BOUND 1
#define SOCKET_STATE_LISTENING 2
#define SOCKET_STATE_CONNECTED 3

#define SOCK_STREAM    1
#define AF_UNIX        1

typedef unsigned socklen_t;
typedef struct {
	void         *msg_name;       
	socklen_t     msg_namelen;    
	struct iovec *msg_iov;        
	size_t        msg_iovlen;     
	void         *msg_control;    
	size_t        msg_controllen; 
	int           msg_flags;      
} msghdr;

typedef struct _socket_t{
	int lock;
	int family;
	int type;
	int protocol;
	struct _socket_t* peer;
	size_t backlogsize;
	off_t backlogend;
	struct _socket_t** backlog;
	event_t connectevent; // when the socket is listening and this is fired, someone used connect() on the socket
	event_t acceptevent;  // when a socket is waiting for a connection to be accepted, it sleeps on this
	event_t dataevent; // fired when data is written to/read from the ring buffer
	int state;
	union {
		sockaddr_un addr_un;
	};
	size_t addrlen;
	ringbuffer_t buffer;
} socket_t;

static inline int socket_addtobacklog(socket_t* listener, socket_t* client){
	if(listener->backlogend == listener->backlogsize)
		return ECONNREFUSED;
	
	listener->backlog[listener->backlogend++] = client;

	return 0;

}

static inline socket_t* socket_popfrombacklog(socket_t* listener){
        
	socket_t* sock = listener->backlog[0];
	listener->backlogend--;

	memcpy(listener->backlog, listener->backlog+1, sizeof(socket_t*)*listener->backlogend);
	
	listener->backlog[listener->backlogend] = NULL;

        return sock;
}

int socket_connect(struct _socket_t* sock, void* addr, socklen_t addrlen, fd_t* fd);
int socket_new(socket_t** returnptr, int family, int type, int protocol);
int socket_bind(struct _socket_t* sock, void* addr, socklen_t addrlen);
int socket_listen(struct _socket_t* sock, int backlog);
int socket_accept(socket_t** peer, socket_t* sock, void* addr, socklen_t* addrlen, fd_t* fd);
int socket_send(socket_t* socket, void* buff, size_t len, int flags, int* error, fd_t* fd);
#endif
