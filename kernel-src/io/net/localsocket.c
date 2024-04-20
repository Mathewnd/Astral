#include <kernel/sock.h>
#include <logging.h>
#include <kernel/alloc.h>
#include <kernel/interrupt.h>
#include <ringbuffer.h>
#include <kernel/vfs.h>

#define SOCKET_BUFFER (256 * 1024)

struct localpair_t;
struct binding_t;
typedef struct {
	socket_t socket;
	char *bindpath;
	ringbuffer_t ringbuffer; // shared with pair
	// client/connected server
	struct localpair_t *pair;
	// listening server
	struct binding_t *binding;
	struct localpair_t **backlog; // shared with binding
	int backlogsize; // shared with binding
	uintmax_t backlogcurrentwrite; // shared with binding
	uintmax_t backlogcurrentread; // shared with binding
} localsocket_t;

// created by the client socket
// stays allocated until the last socket is closing
// in the case of a socket closing while on the backlog, it will remain allocated on the backlog until then
// connected whenever both arent NULL
// the mutex also protect shared data (such as the ringbuffer)
typedef struct localpair_t {
	localsocket_t *server; // from accept()
	localsocket_t *client; // from connect()
	mutex_t mutex;
} localpair_t;

// created on a bind
// binding stays valid as long as both the server socket and vnode are still alive
// keeps a reference to the vnode, but is only deleted from the vnode destructor
// so when the server socket destructor is called, it needs to unref the vnode
// to indicate the binding is no longer used. if the vnode refcount turns 0 then the binding
// will be destroyed.
// see localsock_destroy() (server side) and localsock_leavebinding() (vnode side)
typedef struct binding_t {
	localsocket_t *server;
	vnode_t *vnode;
	mutex_t mutex;
} binding_t;

static inline void pushbacklog(localsocket_t *server, localpair_t *pair) {
	__assert(server->backlogcurrentwrite < server->backlogcurrentread + server->backlogsize);
	server->backlog[server->backlogcurrentwrite++] = pair;
}

static inline localpair_t *popbacklog(localsocket_t *server) {
	__assert(server->backlogcurrentread < server->backlogcurrentwrite);
	localpair_t *pair = server->backlog[server->backlogcurrentread];
	server->backlog[server->backlogcurrentread++] = NULL;
	return pair;
}

static int datapoll(localsocket_t *local, localsocket_t *peer, int events) {
	int revents = 0;

	// data to read?
	if ((events & POLLIN) && RINGBUFFER_DATACOUNT(&local->ringbuffer))
		revents |= POLLIN;

	// space to write?
	if ((events & POLLOUT) && RINGBUFFER_DATACOUNT(&peer->ringbuffer) != SOCKET_BUFFER)
		revents |= POLLOUT;

	return revents;
}

static int listeningpoll(localsocket_t *localsocket, int events) {
	int revents = 0;
	// can insert into backlog?
	if (localsocket->backlogcurrentwrite < localsocket->backlogcurrentread + localsocket->backlogsize)
		revents |= (events & POLLOUT) ? POLLOUT : 0;

	// can accept()?
	if (localsocket->backlogcurrentwrite != localsocket->backlogcurrentread)
		revents |= (events & POLLIN) ? POLLIN : 0;

	return revents;
}

static int clientpoll(localsocket_t *localsocket, int events) {
	localsocket_t *server = localsocket->pair->server;

	// server disconnected
	if (server == NULL)
		return POLLHUP;

	// the client has one more possible poll check:
	// it can still be connecting with the other end
	// so check if server has a backlog. if so, nothing can be done yet
	if (server->backlog)
		return 0;

	return datapoll(localsocket, server, events);
}

static int serverpoll(localsocket_t *localsocket, int events) {
	localsocket_t *client = localsocket->pair->client;

	// client disconnected
	if (client == NULL)
		return POLLHUP;

	return datapoll(localsocket, client, events);
}

// assumes all the needed locks for each case are being held
static int internalpoll(socket_t *socket, polldata_t *data, int events) {
	localsocket_t *localsocket = (localsocket_t *)socket;
	int revents = 0;
	// 4 possible cases:
	// socket is listening
	if (localsocket->backlog)
		 revents = listeningpoll(localsocket, events);

	// socket is client
	else if (localsocket->pair && localsocket->pair->client == localsocket)
		revents = clientpoll(localsocket, events);

	// socket is server
	else if (localsocket->pair && localsocket->pair->server == localsocket)
		revents = serverpoll(localsocket, events);

	// socket is not doing anything (closed/only bound)
	else
		revents = POLLHUP;

	if (revents == 0 && data)
		poll_add(&socket->pollheader, data, events);

	return revents;
}

int localsock_poll(socket_t *socket, polldata_t *data, int events) {
	localsocket_t *localsocket = (localsocket_t *)socket;

	MUTEX_ACQUIRE(&socket->mutex, false);
	if (localsocket->binding)
		MUTEX_ACQUIRE(&localsocket->binding->mutex, false);

	if (localsocket->pair)
		MUTEX_ACQUIRE(&localsocket->pair->mutex, false);

	int revents = internalpoll(socket, data, events);

	if (localsocket->pair)
		MUTEX_RELEASE(&localsocket->pair->mutex);

	if (localsocket->binding)
		MUTEX_RELEASE(&localsocket->binding->mutex);

	MUTEX_RELEASE(&socket->mutex);
	return revents;
}

static int localsock_send(socket_t *socket, sockaddr_t *addr, void *buffer, size_t count, uintmax_t flags, size_t *sendcount) {
	localsocket_t *localsocket = (localsocket_t *)socket;
	MUTEX_ACQUIRE(&socket->mutex, false);
	int error;

	localpair_t *pair = localsocket->pair;
	// socket not connected
	if (pair == NULL) {
		error = ENOTCONN;
		goto leave;
	}

	MUTEX_ACQUIRE(&pair->mutex, false);

	// check/wait for space
	for (;;) {
		polldesc_t desc = {0};
		error = poll_initdesc(&desc, 1);
		if (error)
			goto leave;

		// if the client has closed this will return POLLHUP
		int revents = internalpoll(socket, &desc.data[0], POLLOUT);

		if (revents) {
			poll_leave(&desc);
			poll_destroydesc(&desc);
			break;
		}

		if (flags & V_FFLAGS_NONBLOCKING) {
			error = EAGAIN;
			poll_leave(&desc);
			poll_destroydesc(&desc);
			goto leave;
		}
		MUTEX_RELEASE(&pair->mutex);

		error = poll_dowait(&desc, 0);

		poll_leave(&desc);
		poll_destroydesc(&desc);

		MUTEX_ACQUIRE(&pair->mutex, false);
		if (error)
			goto leave;
	}

	localsocket_t *peer = pair->client == localsocket ? pair->server : pair->client;
	if (peer == NULL) {
		error = EPIPE;
		goto leave;
	}

	*sendcount = ringbuffer_write(&peer->ringbuffer, buffer, count);
	__assert(*sendcount > 0);

	// signal that there is data to read for any threads blocked on the peer socket
	poll_event(&peer->socket.pollheader, POLLIN);

	leave:
	if (pair)
		MUTEX_RELEASE(&pair->mutex);

	MUTEX_RELEASE(&socket->mutex);
	return error;
}

static int localsock_recv(socket_t *socket, sockaddr_t *addr, void *buffer, size_t count, uintmax_t flags, size_t *recvcount) {
	localsocket_t *localsocket = (localsocket_t *)socket;
	MUTEX_ACQUIRE(&socket->mutex, false);
	int error;

	localpair_t *pair = localsocket->pair;
	// socket not connected
	if (pair == NULL) {
		error = ENOTCONN;
		goto leave;
	}

	MUTEX_ACQUIRE(&pair->mutex, false);

	// check/wait for data
	for (;;) {
		polldesc_t desc = {0};
		error = poll_initdesc(&desc, 1);
		if (error)
			goto leave;

		// if the client has closed this will return POLLHUP
		int revents = internalpoll(socket, &desc.data[0], POLLIN);

		if (revents) {
			poll_leave(&desc);
			poll_destroydesc(&desc);
			break;
		}

		if (flags & V_FFLAGS_NONBLOCKING) {
			error = EAGAIN;
			poll_leave(&desc);
			poll_destroydesc(&desc);
			goto leave;
		}
		MUTEX_RELEASE(&pair->mutex);

		error = poll_dowait(&desc, 0);

		poll_leave(&desc);
		poll_destroydesc(&desc);

		MUTEX_ACQUIRE(&pair->mutex, false);
		if (error)
			goto leave;
	}

	*recvcount = ringbuffer_read(&localsocket->ringbuffer, buffer, count);
	localsocket_t *peer = pair->client == localsocket ? pair->server : pair->client;

	// signal that there is space to write for any threads blocked on this socket
	if (peer && *recvcount)
		poll_event(&peer->socket.pollheader, POLLOUT);

	leave:
	if (pair)
		MUTEX_RELEASE(&pair->mutex);

	MUTEX_RELEASE(&socket->mutex);
	return error;
}

static int localsock_accept(socket_t *_server, socket_t *_clientconnection, sockaddr_t *addr, uintmax_t flags) {
	localsocket_t *server = (localsocket_t *)_server;
	localsocket_t *clientconnection = (localsocket_t *)_clientconnection;

	MUTEX_ACQUIRE(&server->socket.mutex, false);
	binding_t *binding = server->binding;
	int error;

	if (binding == NULL) {
		error = EINVAL;
		goto leave;
	}

	MUTEX_ACQUIRE(&server->binding->mutex, false);

	if (server->backlog == NULL) {
		error = EINVAL;
		goto leave;
	}

	localpair_t *pair = NULL;

	// check if there is anything in the backlog
	for (;;) {
		polldesc_t desc = {0};
		error = poll_initdesc(&desc, 1);
		if (error)
			goto leave;

		int revents = internalpoll(&server->socket, &desc.data[0], POLLIN);

		if (revents) {
			// there is something but we need to see if it is valid

			poll_leave(&desc);
			poll_destroydesc(&desc);

			pair = popbacklog(server);
			MUTEX_ACQUIRE(&pair->mutex, false);
			if (pair->client == NULL) {
				// client closed the connection, delete the pair and keep trying
				free(pair);
				continue;
			}
			break;
		}

		if (flags & V_FFLAGS_NONBLOCKING) {
			error = EAGAIN;
			poll_leave(&desc);
			poll_destroydesc(&desc);
			goto leave;
		}

		MUTEX_RELEASE(&binding->mutex);

		error = poll_dowait(&desc, 0);

		poll_leave(&desc);
		poll_destroydesc(&desc);

		if (error)
			goto leave;

		MUTEX_ACQUIRE(&binding->mutex, false);
	}

	// we have a valid pair and we have acquired the mutex already
	// configure the client connection. we don't lock it because it should only be
	// curretly accessible here anyways
	clientconnection->pair = pair;
	pair->server = clientconnection;

	// signal to the client that they can start sending data now
	poll_event(&pair->client->socket.pollheader, POLLOUT);

	MUTEX_RELEASE(&pair->mutex);

	leave:
	if (binding)
		MUTEX_RELEASE(&binding->mutex);

	MUTEX_RELEASE(&server->socket.mutex);
	return error;
}

static int localsock_connect(socket_t *socket, sockaddr_t *addr, uintmax_t flags) {
	localsocket_t *localsocket = (localsocket_t *)socket;
	int error;
	vnode_t *refnode = NULL;
	vnode_t *result = NULL;
	binding_t *binding = NULL;

	MUTEX_ACQUIRE(&socket->mutex, false);
	// listening/bound socket
	if (localsocket->backlog || localsocket->binding) {
		error = EOPNOTSUPP;
		goto leave;
	}

	// connected socket
	if (localsocket->pair) {
		error = EISCONN;
		goto leave;
	}

	// get the vnode specified in addr
	refnode = addr->path[0] == '/' ? sched_getroot() : sched_getcwd();
	error = vfs_lookup(&result, refnode, addr->path, NULL, 0);
	if (error)
		goto leave;

	// check if its a socket node
	if (result->type != V_TYPE_SOCKET) {
		error = ENOTSOCK;
		goto leave;
	}

	// and if it has a valid binding
	binding = result->socketbinding;
	if (binding == NULL) {
		error = ECONNREFUSED;
		goto leave;
	}

	// and if the server has not closed the socket and is listening
	MUTEX_ACQUIRE(&binding->mutex, false);
	localsocket_t *server = binding->server;
	if (server == NULL || server->backlog == NULL) {
		error = ECONNREFUSED;
		goto leave;
	}

	// we have a listening socket! we have to add ourselves to the backlog now.

	// check if there is space to add in the backlog
	for (;;) {
		polldesc_t desc = {0};
		error = poll_initdesc(&desc, 1);
		if (error)
			goto leave;

		int revents = internalpoll(&server->socket, &desc.data[0], POLLOUT);

		if (revents) {
			poll_leave(&desc);
			poll_destroydesc(&desc);
			break;
		}

		if (flags & V_FFLAGS_NONBLOCKING) {
			error = EAGAIN;
			poll_leave(&desc);
			poll_destroydesc(&desc);
			goto leave;
		}

		// to make sure the binding doesn't get destroyed once we stop locking it, reference the vnode
		VOP_HOLD(binding->vnode);
		MUTEX_RELEASE(&binding->mutex);

		error = poll_dowait(&desc, 0);

		poll_leave(&desc);
		poll_destroydesc(&desc);

		if (error)
			goto leave;

		MUTEX_ACQUIRE(&binding->mutex, false);
		if (binding->server == NULL) {
			// server closed while we waited!
			// release the vnode so it can get deleted and set the binding to NULL as we can't be sure on its state anymore.
			VOP_RELEASE(binding->vnode);
			binding = NULL;
			error = ECONNREFUSED;
			goto leave;
		}
		VOP_RELEASE(binding->vnode);
	}

	localpair_t *pair = alloc(sizeof(localpair_t));
	if (pair == NULL) {
		error = ENOMEM;
		goto leave;
	}

	pair->server = server;
	pair->client = localsocket;
	MUTEX_INIT(&pair->mutex);
	MUTEX_ACQUIRE(&pair->mutex, false);

	pushbacklog(server, pair);

	// signal to the server they can accept() now
	poll_event(&server->socket.pollheader, POLLIN);

	// we will not use any more binding related shared data, unlock it.
	// we still hold the lock over the pair, though. this will prevent both the server
	// being destroyed and accepting us before we're ready.
	// also NULL the binding to indicate that its state will be uncertain from now on.
	MUTEX_RELEASE(&binding->mutex);
	binding = NULL;

	localsocket->pair = pair;

	// if nonblocking, return success. the userspace/whatever will
	// poll() the client to know when they can send data or if the server gave up and closed
	if (flags & V_FFLAGS_NONBLOCKING) {
		error = 0;
		goto leave; 
	}

	// otherwise, we will just do the same thing as userspace (but here)
	polldesc_t desc = {0};
	error = poll_initdesc(&desc, 1);
	if (error)
		goto leave;

	int revents = internalpoll(socket, &desc.data[0], POLLOUT);
	__assert(revents == 0);

	// release and wait. once we return, we will be connected.
	MUTEX_RELEASE(&pair->mutex);

	error = poll_dowait(&desc, 0);

	poll_leave(&desc);
	poll_destroydesc(&desc);

	if (error)
		goto leave;

	leave:
	if (binding)
		MUTEX_RELEASE(&binding->mutex);

	if (result)
		VOP_RELEASE(result);

	if (refnode)
		VOP_RELEASE(refnode);

	MUTEX_RELEASE(&socket->mutex);
	return error;
}

static int localsock_listen(socket_t *socket, int backlogsize) {
	localsocket_t *localsocket = (localsocket_t *)socket;
	MUTEX_ACQUIRE(&socket->mutex, false);
	
	int error = 0;
	// already listening
	if (localsocket->backlog)
		goto leave;

	// connected/unbound socket
	if (localsocket->pair || localsocket->binding == NULL) {
		error = EOPNOTSUPP;
		goto leave;
	}

	MUTEX_ACQUIRE(&localsocket->binding->mutex, false);

	localsocket->backlog = alloc(backlogsize * sizeof(localpair_t *));
	if (localsocket->backlog == NULL) {
		MUTEX_RELEASE(&localsocket->binding->mutex);
		error = ENOMEM;
		goto leave;
	}

	localsocket->backlogsize = backlogsize;

	MUTEX_RELEASE(&localsocket->binding->mutex);

	leave:
	MUTEX_RELEASE(&socket->mutex);
	return error;
}

static void localsock_destroy(socket_t *socket) {
	localsocket_t *localsocket = (localsocket_t *)socket;

	// disconnect the socket (connected/connecting sockets)
	localpair_t *pair = localsocket->pair;
	if (localsocket->pair) {
		MUTEX_ACQUIRE(&pair->mutex, false);
		localsocket_t **peerp;
		localsocket_t **ourp;

		if (pair->server == localsocket) {
			peerp = &pair->client;
			ourp = &pair->server;
		} else if (pair->client == localsocket) {
			peerp = &pair->server;
			ourp = &pair->client;
		} else {
			__assert(!"freeing socket which is neither client nor server");
		}

		if (*peerp == NULL) {
			free(pair);
		} else {
			*ourp = NULL;
			MUTEX_RELEASE(&pair->mutex);
		}
	}

	// leave binding (bound socket)
	binding_t *binding = localsocket->binding;
	if (binding) {
		MUTEX_ACQUIRE(&binding->mutex, false);
		__assert(binding->vnode);
		binding->server = NULL;
		MUTEX_RELEASE(&binding->mutex);
		VOP_RELEASE(binding->vnode);
	}

	// destroy backlog (for listening sockets)
	if (localsocket->backlog) {
		for (int i = 0; i < localsocket->backlogsize; ++i) {
			localpair_t *pair = localsocket->backlog[i];
			if (pair == NULL)
				continue;

			MUTEX_ACQUIRE(&pair->mutex, false);
			if (pair->client == NULL) {
				// client gave up
				free(pair);
			} else {
				// client still trying to connect
				pair->server = NULL;
				MUTEX_RELEASE(&pair->mutex);
			}
		}

		free(localsocket->backlog);
		localsocket->backlog = NULL;
	}

	poll_event(&socket->pollheader, POLLHUP);

	// the peer only accesses the socket while the pair mutex is acquired
	// since its gone now, we don't have to worry about the other end accessing it

	if (localsocket->bindpath)
		free(localsocket->bindpath);

	ringbuffer_destroy(&localsocket->ringbuffer);
	free(socket);
}

static int localsock_bind(socket_t *socket, sockaddr_t *addr) {
	MUTEX_ACQUIRE(&socket->mutex, false);
	int error;
	char *path = NULL;
	vnode_t *refnode = NULL;

	binding_t *binding = alloc(sizeof(binding_t));
	if (binding == NULL)
		return ENOMEM;
	
	path = alloc(strlen(addr->path) + 1);
	if (path == NULL) {
		error = ENOMEM;
		goto cleanup;
	}

	strcpy(path, addr->path);

	refnode = *path == '/' ? sched_getroot() : sched_getcwd();
	vattr_t attr = {
		.mode = _cpu()->thread->proc ? UMASK(0777) : 0644,
		.gid = _cpu()->thread->proc ? _cpu()->thread->proc->cred.gid : 0,
		.uid = _cpu()->thread->proc ? _cpu()->thread->proc->cred.uid : 0
	};

	vnode_t *vnode;
	error = vfs_create(refnode, path, &attr, V_TYPE_SOCKET, &vnode);
	if (error)
		goto cleanup;

	localsocket_t *localsocket = (localsocket_t *)socket;
	binding->vnode = vnode;
	binding->server = localsocket;
	MUTEX_INIT(&binding->mutex);

	VOP_LOCK(vnode);
	vnode->socketbinding = binding;
	VOP_UNLOCK(vnode);
	localsocket->binding = binding;

	cleanup:
	if (error) {
		free(path);
		if (binding)
			free(binding);
	}

	if (refnode) {
		VOP_RELEASE(refnode);
	}

	MUTEX_RELEASE(&socket->mutex);
	return error;
}

static size_t localsocket_datacount(socket_t *socket) {
	localsocket_t *localsocket = (localsocket_t *)socket;
	size_t nbytes = 0;
	MUTEX_ACQUIRE(&socket->mutex, false);
	if (localsocket->pair == NULL) 
		goto cleanup;

	MUTEX_ACQUIRE(&localsocket->pair->mutex, false);
	nbytes = RINGBUFFER_DATACOUNT(&localsocket->ringbuffer);
	MUTEX_RELEASE(&localsocket->pair->mutex);

	cleanup:
	MUTEX_RELEASE(&socket->mutex);
	return nbytes;
}

// only called on vnode remove (vfs_inactive)
void localsock_leavebinding(vnode_t *vnode) {
	binding_t *binding = vnode->socketbinding;
	MUTEX_ACQUIRE(&binding->mutex, false);
	if (binding->server) {
		// server socket still exists, so don't free the structure and only remove the vnode pointer from it
		binding->vnode = NULL;
		MUTEX_RELEASE(&binding->mutex);
	} else {
		// server socket is gone, destroy the binding structure
		free(binding);
	}

	// not really needed but added just to make sure
	vnode->socketbinding = NULL;
}

static socketops_t socketops = {
	.bind = localsock_bind,
	.listen = localsock_listen,
	.connect = localsock_connect,
	.accept = localsock_accept,
	.send = localsock_send,
	.recv = localsock_recv,
	.destroy = localsock_destroy,
	.poll = localsock_poll,
	.datacount = localsocket_datacount
};

socket_t *localsock_createsocket() {
	// XXX possibly move this to a slab?
	localsocket_t *socket = alloc(sizeof(localsocket_t));
	if (socket == NULL)
		return NULL;

	// TODO would waste resources on listening sockets
	if (ringbuffer_init(&socket->ringbuffer, SOCKET_BUFFER)) {
		free(socket);
		return NULL;
	}

	socket->socket.ops = &socketops;

	return (socket_t *)socket;
}
