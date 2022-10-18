#include <kernel/socket.h>
#include <kernel/unsocket.h>
#include <errno.h>
#include <kernel/alloc.h>
#include <arch/spinlock.h>

int socket_new(struct _socket_t** returnptr, int family, int type, int protocol){

	switch(family){
		case AF_UNIX:
			return unsocket_new(returnptr, type, protocol);
			break;
		default:
			return EAFNOSUPPORT;
	}

}

// TODO move around locks and tests

int socket_bind(struct _socket_t* sock, void* addr, size_t addrlen){
	
	if(sock->addrlen)
		return EINVAL;

	switch(sock->family){
		case AF_UNIX:
			return unsocket_bind(sock, addr, addrlen);
		default:
			return EINVAL;
	}
}

int socket_listen(struct _socket_t* sock, int backlog){
	
	// XXX check if the socket supports listening?
	
	int err = 0;

	spinlock_acquire(&sock->lock);
	
	// XXX limit backlog

	if(sock->state != SOCKET_STATE_BOUND || backlog == 0){
		err = EINVAL;
		goto _ret;
	}
	
	sock->backlog = alloc(backlog*sizeof(socket_t**));
	
	if(!sock->backlog){
		err = ENOMEM;
		goto _ret;
	}
	
	sock->backlogsize = backlog;
	sock->state = SOCKET_STATE_LISTENING;
	
	_ret:

	spinlock_release(&sock->lock);

	return err;

}

int socket_connect(struct _socket_t* sock, void* addr, size_t addrlen){
	
	switch(sock->family){
		case AF_UNIX:
			return unsocket_connect(sock, addr, addrlen);
		default:
			return EINVAL;
	}
	
}

int socket_accept(socket_t** peer, socket_t* sock, void* addr, size_t* addrlen){
	switch(sock->family){
		case AF_UNIX:
			return unsocket_accept(peer, sock, addr, addrlen);
		default:
			return EINVAL;
	}
}

int socket_send(socket_t* socket, void* buff, size_t len, int flags, int* count){
	switch(socket->family){
		case AF_UNIX:
			return unsocket_send(socket, buff, len, flags, count);
		default:
			return EINVAL;
	}
}
