#include <kernel/syscalls.h>
#include <kernel/socket.h>
#include <kernel/fd.h>
#include <arch/cls.h>
#include <arch/spinlock.h>

extern fs_t kerneltmpfs;

syscallret syscall_accept(int sockfd, void* addr, socklen_t* addrlen){

	syscallret retv;
        retv.ret = -1;

        if(addr > USER_SPACE_END){
                retv.errno = EFAULT;
                return retv;
        }

        proc_t* proc = arch_getcls()->thread->proc;

	// access sockfd

        fd_t* fd;

        int err = fd_access(&proc->fdtable, &fd, sockfd);

        if(err){
                retv.errno = err;
                return retv;
        }

        if(GETTYPE(fd->mode) != TYPE_SOCKET){
                retv.errno = ENOTSOCK;
                goto _fail;
        }

	socket_t* listenersock = fd->node->objdata;

	// create new fd

	fd_t* connfd = NULL;
	int   connifd;

	retv.errno = fd_alloc(&proc->fdtable, &connfd, &connifd, 0);

	if(retv.errno)
		goto _fail;
	
	connfd->node = vfs_newnode("SOCKET", &kerneltmpfs, NULL);
        if(!connfd->node){
                retv.errno = ENOMEM;
                goto _fail;
        }
        connfd->mode = 0777 | MAKETYPE(TYPE_SOCKET);
        connfd->node->refcount = 1;
        connfd->node->st.st_mode = connfd->mode;

	// create socket for new fd

	socket_t* connsock;

	retv.errno = socket_new(&connsock, listenersock->family, listenersock->type, listenersock->protocol);

	// get peer

	socket_t* peer;

        retv.errno = socket_accept(&peer, listenersock, addr, addrlen);

	if(retv.errno)
		goto _fail;
	
	connsock->peer = peer;
	connsock->state = SOCKET_STATE_CONNECTED;

	spinlock_acquire(&peer->lock);
	
	peer->peer = connsock;

	peer->state = SOCKET_STATE_CONNECTED;
	event_signal(&peer->acceptevent, true);
	
	connfd->node->objdata = connsock;

	
	spinlock_release(&peer->lock);

        fd_release(fd);
	fd_release(connfd);
	
	retv.ret = connifd;

	memcpy(addr, &peer->addr_un, peer->addrlen);
	*addrlen = peer->addrlen;

        return retv;
	
	_fail:
	
        fd_release(fd);

	if(connfd){
		fd_release(connfd);
		fd_free(&proc->fdtable, connifd);
	}

	return retv;
	
	
}
