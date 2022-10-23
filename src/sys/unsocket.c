#include <kernel/socket.h>
#include <kernel/unsocket.h>
#include <kernel/alloc.h>
#include <arch/cls.h>
#include <errno.h>
#include <arch/spinlock.h>
#include <string.h>
#include <kernel/vfs.h>
#include <arch/interrupt.h>
#include <poll.h>

int unsocket_new(socket_t** returnptr, int type, int protocol){
	
	if(type != SOCK_STREAM)
		return EINVAL;
	
	socket_t* socket = alloc(sizeof(socket_t));
	
	if(!socket)
		return ENOMEM;
	
	socket->family = AF_UNIX;
	socket->type = type;
	socket->protocol = protocol;
	socket->state = SOCKET_STATE_UNBOUND;
	if(ringbuffer_init(&socket->buffer, 1024*128)){ // 128kb for socket buff
		free(socket);
		return ENOMEM;
	}
	
	*returnptr = socket;

	return 0;
	
}

int unsocket_bind(struct _socket_t* sock, sockaddr_un* addr, socklen_t addrlen){
	
	// copy address

	if(addrlen > sizeof(sockaddr_un))
		return EINVAL;

	sockaddr_un unaddr;

	memcpy(&unaddr, addr, sizeof(sockaddr_un));

	if(unaddr.sun_family != AF_UNIX)
		return EINVAL;

	// create and prepare the socket node

	spinlock_acquire(&sock->lock);
	
	proc_t* proc = arch_getcls()->thread->proc;

	int err = vfs_mksocket(unaddr.sun_path[0] == '/' ? proc->root : proc->cwd, unaddr.sun_path, 0777 & ~proc->umask);

	if(err)
		goto _return;

	vnode_t* socketnode;

	err = vfs_open(&socketnode, unaddr.sun_path[0] == '/' ? proc->root : proc->cwd, unaddr.sun_path);

	if(err)
		goto _return;
	
	socketnode->objdata = sock;

	sock->addr_un = unaddr;
	sock->addrlen = sizeof(sockaddr_un);
	sock->state = SOCKET_STATE_BOUND;
	err = 0;

	_return: 
	
	spinlock_release(&sock->lock);
	
	return err;

}

int unsocket_connect(struct _socket_t* sock, void* addr, socklen_t addrlen){

	// copy address

	socket_t* peer = NULL;

	if(addrlen > sizeof(sockaddr_un))
		return EINVAL;

	sockaddr_un addr_un;
	memcpy(&addr_un, addr, sizeof(sockaddr_un));
	
	if(addr_un.sun_family != AF_UNIX)
		return EAFNOSUPPORT;

	// do a few checks on the socket to see if the request is valid
	
	proc_t* proc = arch_getcls()->thread->proc;

	int err = 0;

	spinlock_acquire(&sock->lock);

	if(sock->state == SOCKET_STATE_LISTENING){
		err = EINVAL;
		goto _return;
	}

	if(sock->state == SOCKET_STATE_CONNECTED){
		err = EISCONN;
		goto _return;
	}

	vnode_t* vnode = NULL;

	err = vfs_open(&vnode, addr_un.sun_path[0] == '/' ? proc->root : proc->cwd, addr_un.sun_path);

	if(err)
		goto _return;
	
	if(GETTYPE(vnode->st.st_mode) != TYPE_SOCKET){
		err = ENOTSOCK;
		goto _return;
	}

	peer = vnode->objdata;
	
	spinlock_acquire(&peer->lock);

	if(peer->type != sock->type){
		err = EPROTOTYPE;
		goto _return;
	}

	if(peer->state != SOCKET_STATE_LISTENING){
		err = ECONNREFUSED;
		goto _return;
	}
	
	

	err = socket_addtobacklog(peer, sock);
	if(err)
		goto _return;

	arch_interrupt_disable();
	event_signal(&peer->connectevent, false);
	
	spinlock_release(&peer->lock);
	spinlock_release(&sock->lock);
	
	err = event_wait(&sock->acceptevent, true);
	
	spinlock_acquire(&peer->lock);
	spinlock_acquire(&sock->lock);

		// TODO remove from backlog!
	if(err)
		goto _return;
	
	
	_return:

	if(peer)
		spinlock_release(&peer->lock);

	if(vnode)
		vfs_close(vnode);
	
	spinlock_release(&sock->lock);

	return err;
	
}

int unsocket_accept(socket_t** peer, socket_t* sock, void* addr, socklen_t* addrlen){
	
	// TODO abort

	socklen_t len = *addrlen;

	int err = 0;

	if(*addrlen > sizeof(sockaddr_un))
                return EINVAL;

	spinlock_acquire(&sock->lock);

	if(sock->state != SOCKET_STATE_LISTENING){
		err = EINVAL;
		goto _return;
	}

	// TODO interruptble

	while(!sock->backlogend){
		spinlock_release(&sock->lock);
		event_wait(&sock->connectevent, true);
		spinlock_acquire(&sock->lock);
	}
	
	*peer = socket_popfrombacklog(sock);

	spinlock_release(&sock->lock);

	_return:

	return err;
	
}

int unsocket_send(socket_t* socket, void* buff, socklen_t len, int flags, int* error){

	if(flags){ // right now no flags are supported
		*error = EOPNOTSUPP;
		return 0;
	}

	int writec = 0;

	socket_t* peer = socket->peer;

	spinlock_acquire(&socket->lock);
	spinlock_acquire(&peer->lock);

	if(socket->state != SOCKET_STATE_CONNECTED){
		*error = ENOTCONN;
		goto _return;
	}

	for(;;){
		arch_interrupt_disable();
		
		writec = ringbuffer_write(&peer->buffer, buff, len);
		
		if(writec > 0){
			event_signal(&peer->dataevent, true);
			break;
		}

		spinlock_release(&peer->lock);
		
		if(event_wait(&peer->dataevent, true)){
			spinlock_acquire(&peer->lock);
			writec = -1;
			break;
		}

		spinlock_acquire(&peer->lock);

	}
	
	*error = 0;
	
	_return:

	spinlock_release(&peer->lock);
	spinlock_release(&socket->lock);

	return writec;

}

// TODO check for broken

int unsocket_recv(socket_t* socket, void* buff, socklen_t len, int flags, int* error){
	
	if(flags){ // right now no flags are supported
                *error = EOPNOTSUPP;
                return 0;
        }
	
	int readc = 0;

	spinlock_acquire(&socket->lock);

	if(socket->state != SOCKET_STATE_CONNECTED){
		*error = ENOTCONN;
		goto _return;
	}
	
	for(;;){
		
		arch_interrupt_disable();

		readc = ringbuffer_read(&socket->buffer, buff, len);

		if(readc > 0){
			event_signal(&socket->dataevent, true);
			break;
		}

		spinlock_release(&socket->lock);

		if(event_wait(&socket->dataevent, true)){
			spinlock_acquire(&socket->lock);
			readc = -1;
			*error = EINTR;
			break;
		}

		spinlock_acquire(&socket->lock);
		
	}

	*error = 0;

	_return:

	spinlock_release(&socket->lock);
	
	return readc;
}

int unsocket_poll(socket_t* socket, pollfd* fd){
	spinlock_acquire(&socket->lock);

	// TODO outgoing connect finished

	switch(socket->state){
		case SOCKET_STATE_LISTENING:
			if(fd->events & POLLIN && socket->backlogend)
				fd->revents |= POLLIN;

			break;
		case SOCKET_STATE_CONNECTED:
			
			if(!socket->peer)
				fd->revents |= POLLHUP;

			if(fd->events & POLLIN && socket->buffer.write != socket->buffer.read)
                        	fd->revents |= POLLIN;

			if(fd->events & POLLOUT && socket->buffer.write != socket->buffer.read + socket->buffer.size)
				fd->revents |= POLLOUT;
			
			break;
		default:
			return 0;
	}

	spinlock_release(&socket->lock);
	return 0;
}

