#include <kernel/socket.h>
#include <kernel/unsocket.h>
#include <kernel/alloc.h>
#include <arch/cls.h>
#include <errno.h>
#include <arch/spinlock.h>
#include <string.h>
#include <kernel/vfs.h>
#include <arch/interrupt.h>

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

int unsocket_bind(struct _socket_t* sock, sockaddr_un* addr, size_t addrlen){
	
	// copy address

	if(addrlen > sizeof(sockaddr_un))
		return EINVAL;

	sockaddr_un unaddr;

	memcpy(&unaddr, addr, addrlen);

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
	sock->addrlen = addrlen;
	sock->state = SOCKET_STATE_BOUND;
	err = 0;

	_return: 
	
	spinlock_release(&sock->lock);
	
	return err;

}

int unsocket_connect(struct _socket_t* sock, void* addr, size_t addrlen){

	// copy address

	socket_t* peer = NULL;

	if(addrlen > sizeof(sockaddr_un))
		return EINVAL;

	sockaddr_un addr_un;
	memcpy(&addr_un, addr, addrlen);
	
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

int unsocket_accept(socket_t** peer, socket_t* sock, void* addr, size_t* addrlen){
	
	// TODO abort

	size_t len = *addrlen;

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
