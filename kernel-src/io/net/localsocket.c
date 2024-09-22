#include <kernel/sock.h>
#include <logging.h>
#include <kernel/alloc.h>
#include <kernel/interrupt.h>
#include <ringbuffer.h>
#include <kernel/vfs.h>
#include <kernel/file.h>

#define SOCKET_BUFFER (256 * 1024)
#define FILE_COUNT 16
#define BARRIER_SIZE 16

struct localpair_t;
struct binding_t;
typedef struct {
	socket_t socket;
	ringbuffer_t ringbuffer; // shared with pair
	// client/connected server
	struct localpair_t *pair;
	// listening server
	struct binding_t *binding;
	struct localpair_t **backlog; // shared with binding
	int backlogsize; // shared with binding
	uintmax_t backlogcurrentwrite; // shared with binding
	uintmax_t backlogcurrentread; // shared with binding
	char *bindpath;
	ringbuffer_t fds; // shared with pair
	uintmax_t barriercurrent; // shared with pair
	uintmax_t barrierwrite; // shared with pair
	int filesremaining[BARRIER_SIZE]; // shared with pair
	int bytesremaining[BARRIER_SIZE]; // shared with pair
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

#define LOCALSOCK_CTRL_FILE 1

static int sendctrl(socket_t *socket, sockctrl_t *ctrl, size_t len, size_t datasent) {
	localsocket_t *localsocket = (localsocket_t *)socket;
	localsocket_t *peer = localsocket->pair->client == localsocket ? localsocket->pair->server : localsocket->pair->client;
	if (ctrl == NULL) {
		// if there is no control data, there is no need to add a barrier
		// increase the bytes in the current section
		peer->bytesremaining[peer->barrierwrite] += datasent;
		return 0;
	}

	if (peer == NULL)
		return 0;

	size_t ctrlcount = sock_countctrl(ctrl, len);
	if (ctrlcount == 0)
		return 0;

	int error = 0;

	for (int i = 0; i < ctrlcount; ++i) {
		if (ctrl->level != SOL_SOCKET)
			continue;

		if (ctrl->type == LOCALSOCK_CTRL_FILE) {
			int *fds = (int *)ctrl->data;
			size_t fdcount = (ctrl->length - sizeof(sockctrl_t)) / sizeof(int);

			// if theres no space in the ringbuffer for all fds, return ENOMEM
			// NOTE: I wasn't able to find what happens in this case in other implementations,
			// so this behaviour is likely wrong but suffices for now
			if (fdcount > RINGBUFFER_FREESPACE(&peer->fds) / sizeof(file_t *)) {
				error = ENOMEM;
				break;
			}

			file_t *files[fdcount];
			memset(files, 0, fdcount);

			for (int i = 0; i < fdcount; ++i) {
				files[i] = fd_get(fds[i]);
				if (files[i] == NULL) {
					error = EBADF;
					break;
				}
			}

			if (error) {
				// release all fds we acquired in case of error
				for (int i = 0; i < fdcount; ++i) {
					if (files[i] == NULL)
						break;

					fd_release(files[i]);
				}
				break;
			}

			// write it to the fd ring buffer
			__assert(ringbuffer_write(&peer->fds, files, fdcount * sizeof(file_t *)) == fdcount * sizeof(file_t *));

			// create a new barrier secion
			__assert(peer->barrierwrite != peer->barriercurrent + BARRIER_SIZE);
			peer->barrierwrite++;
			peer->filesremaining[peer->barrierwrite % BARRIER_SIZE] = fdcount;
			peer->bytesremaining[peer->barrierwrite % BARRIER_SIZE] = datasent;
		}

		ctrl = SOCK_CTRL_NEXT(ctrl);
	}

	return error;
}

static int recvctrl(socket_t *socket, sockctrl_t *ctrl, size_t len, bool *truncated, size_t *receivedlen) {
	localsocket_t *localsocket = (localsocket_t *)socket;
	int error = 0;
	*receivedlen = 0;

	// ctlr can be null to discard any control data
	if (ctrl != NULL) {
		// files in the buffer
		size_t filecount = RINGBUFFER_DATACOUNT(&localsocket->fds) / sizeof(file_t *);

		// actual number that actually will be read will be the smallest of one of the following:
		// - number fds that fit in the data of the control
		// - files in the current section
		// - files actually in the ringbuffer
		size_t actualcount = min(min((len - sizeof(sockctrl_t)) / sizeof(int), filecount), localsocket->filesremaining[localsocket->barriercurrent]);
		if (actualcount == 0)
			goto discard;

		size_t donecount = 0;
		int fds[actualcount];

		for (donecount = 0; donecount < actualcount; ++donecount) {
			// read the file pointers and insert them into the process fd table
			file_t *file;
			__assert(ringbuffer_read(&localsocket->fds, &file, sizeof(file_t *)) == sizeof(file_t *));

			if (fd_insert(file, &fds[donecount])) {
				fd_release(file);
				break;
			}

			// fd_insert adds a reference, release ours here
			fd_release(file);
		}

		// prepare the ctrl that will be returned
		ctrl->length = donecount * sizeof(int) + sizeof(sockctrl_t);
		ctrl->level = SOL_SOCKET;
		ctrl->type = LOCALSOCK_CTRL_FILE;
		memcpy(ctrl->data, fds, donecount * sizeof(int));

		// prepare for discarding the rest later
		localsocket->filesremaining[localsocket->barriercurrent] -= donecount;

		*receivedlen += ctrl->length;
	}

	discard:
	// discard the rest
	file_t *file = NULL;
	for (int i = 0; i < localsocket->filesremaining[localsocket->barriercurrent]; ++i) {
		ringbuffer_read(&localsocket->fds, &file, sizeof(file_t *));
		fd_release(file);
	}

	*truncated = file != NULL;

	localsocket->filesremaining[localsocket->barriercurrent] = 0;

	return error;
}

static int localsock_send(socket_t *socket, sockdesc_t *sockdesc) {
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

		if (sockdesc->flags & V_FFLAGS_NONBLOCKING) {
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
		if (current_thread()->proc && (sockdesc->flags & SOCKET_SEND_FLAGS_NOSIGNAL) == 0)
			signal_signalproc(current_thread()->proc, SIGPIPE);

		error = EPIPE;
		goto leave;
	}

	sockdesc->donecount = iovec_iterator_write_to_ringbuffer(sockdesc->iovec_iterator, &peer->ringbuffer, sockdesc->count);
	if (sockdesc->donecount == RINGBUFFER_USER_COPY_FAILED) {
		error = EFAULT;
		goto leave;
	}

	__assert(sockdesc->donecount > 0);

	error = sendctrl(socket, sockdesc->ctrl, sockdesc->ctrllen, sockdesc->donecount);
	if (error) {
		// TODO remove the data that was just sent
		goto leave;
	}

	// signal that there is data to read for any threads blocked on the peer socket
	poll_event(&peer->socket.pollheader, POLLIN);

	leave:
	if (pair)
		MUTEX_RELEASE(&pair->mutex);

	MUTEX_RELEASE(&socket->mutex);
	return error;
}


static int localsock_recv(socket_t *socket, sockdesc_t *sockdesc) {
	localsocket_t *localsocket = (localsocket_t *)socket;
	uintmax_t flags = sockdesc->flags;
	sockdesc->flags = 0;
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
			if ((revents & POLLHUP) == 0 && (flags & SOCKET_RECV_FLAGS_WAITALL) &&
				(flags & SOCKET_RECV_FLAGS_PEEK) == 0 && RINGBUFFER_DATACOUNT(&localsocket->ringbuffer) < sockdesc->count) {
				// wait for more data (WAITALL is set)
				poll_add(&socket->pollheader, &desc.data[0], POLLIN);
			} else {
				poll_leave(&desc);
				poll_destroydesc(&desc);
				break;
			}
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

	// receive control data
	bool truncated;
	error = recvctrl(socket, sockdesc->ctrl, sockdesc->ctrllen, &truncated, &sockdesc->ctrldone);
	if (error)
		goto leave;

	if (truncated)
		sockdesc->flags |= SOCKET_RECV_FLAGS_CTRLTRUNCATED;

	size_t recvcount = min(sockdesc->count, RINGBUFFER_DATACOUNT(&localsocket->ringbuffer));

	// check for a barrier from control data
	if (localsocket->bytesremaining[localsocket->barriercurrent % BARRIER_SIZE]) {
		recvcount = min(recvcount, localsocket->bytesremaining[localsocket->barriercurrent]);

		// check if we should step forward from that barrier
		localsocket->bytesremaining[localsocket->barriercurrent % BARRIER_SIZE] -= recvcount;
		if (localsocket->bytesremaining[localsocket->barriercurrent % BARRIER_SIZE] == 0)
			++localsocket->barriercurrent;
	}

	if (flags & SOCKET_RECV_FLAGS_PEEK) {
		sockdesc->donecount = iovec_iterator_peek_from_ringbuffer(sockdesc->iovec_iterator, &localsocket->ringbuffer, 0, recvcount);
		if (sockdesc->donecount == RINGBUFFER_USER_COPY_FAILED) {
			error = EFAULT;
			goto leave;
		}
	} else {
		sockdesc->donecount = iovec_iterator_read_from_ringbuffer(sockdesc->iovec_iterator, &localsocket->ringbuffer, recvcount);
		if (sockdesc->donecount == RINGBUFFER_USER_COPY_FAILED) {
			error = EPIPE;
			goto leave;
		}

		localsocket_t *peer = pair->client == localsocket ? pair->server : pair->client;
		// signal that there is space to write for any threads blocked on this socket
		if (peer && sockdesc->donecount)
			poll_event(&peer->socket.pollheader, POLLOUT);
	}

	__assert(sockdesc->donecount == recvcount);

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

static int localsock_connect(socket_t *socket, sockaddr_t *addr, uintmax_t flags, cred_t *cred) {
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
	refnode = addr->path[0] == '/' ? proc_get_root() : proc_get_cwd();
	error = vfs_lookup(&result, refnode, addr->path, NULL, 0);
	if (error)
		goto leave;

	// check if its a socket node
	if (result->type != V_TYPE_SOCKET) {
		error = ENOTSOCK;
		// locked by vfs_lookup
		VOP_UNLOCK(result);
		goto leave;
	}

	// and we have write access
	error = VOP_ACCESS(result, V_ACCESS_WRITE, cred);
	if (error) {
		// locked by vfs_lookup
		VOP_UNLOCK(result);
		goto leave;
	}

	// and if it has a valid binding
	binding = result->socketbinding;
	// locked by vfs_lookup
	VOP_UNLOCK(result);
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

	// discard any files in-flight
	file_t *file = NULL;
	while (ringbuffer_read(&localsocket->fds, &file, sizeof(file_t *)))
		fd_release(file);

	ringbuffer_destroy(&localsocket->fds);
	ringbuffer_destroy(&localsocket->ringbuffer);
	free(socket);
}

static int localsock_bind(socket_t *socket, sockaddr_t *addr, cred_t *cred) {
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

	refnode = *path == '/' ? proc_get_root() : proc_get_cwd();
	vattr_t attr = {
		.mode = current_thread()->proc ? UMASK(0777) : 0644,
		.gid = current_thread()->proc ? current_thread()->proc->cred.gid : 0,
		.uid = current_thread()->proc ? current_thread()->proc->cred.uid : 0
	};

	vnode_t *vnode;
	error = vfs_create(refnode, path, &attr, V_TYPE_SOCKET, &vnode);
	if (error)
		goto cleanup;

	localsocket_t *localsocket = (localsocket_t *)socket;
	binding->vnode = vnode;
	binding->server = localsocket;
	MUTEX_INIT(&binding->mutex);

	vnode->socketbinding = binding;
	// locked by vfs_create
	VOP_UNLOCK(vnode);
	localsocket->binding = binding;
	localsocket->bindpath = path;

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

int localsock_pair(socket_t **ret1, socket_t **ret2) {
	socket_t *socket1 = socket_create(SOCKET_TYPE_LOCAL);
	if (socket1 == NULL)
		return ENOMEM;

	socket_t *socket2 = socket_create(SOCKET_TYPE_LOCAL);
	if (socket2 == NULL) {
		localsock_destroy(socket1);
		return ENOMEM;
	}

	localpair_t *pair = alloc(sizeof(localpair_t));
	if (pair == NULL) {
		localsock_destroy(socket1);
		localsock_destroy(socket2);
		return ENOMEM;
	}

	MUTEX_INIT(&pair->mutex);
	pair->server = (localsocket_t *)socket1;
	pair->client = (localsocket_t *)socket2;
	((localsocket_t *)socket1)->pair = pair;
	((localsocket_t *)socket2)->pair = pair;

	*ret1 = socket1;
	*ret2 = socket2;
	return 0;
}

static int localsock_getname(socket_t *socket, sockaddr_t *addr) {
	localsocket_t *localsocket = (localsocket_t *)socket;
	MUTEX_ACQUIRE(&socket->mutex, false);
	if (localsocket->bindpath)
		strcpy(addr->path, localsocket->bindpath);
	else
		addr->path[0] = '\0';

	MUTEX_RELEASE(&socket->mutex);
	return 0;
}

static int localsock_getpeername(socket_t *socket, sockaddr_t *addr) {
	addr->path[0] = '\0';
	return 0;
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
	.datacount = localsocket_datacount,
	.getname = localsock_getname,
	.getpeername = localsock_getpeername
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

	if (ringbuffer_init(&socket->fds, FILE_COUNT * sizeof(file_t *))) {
		ringbuffer_destroy(&socket->ringbuffer);
		free(socket);
		return NULL;
	}

	socket->socket.ops = &socketops;

	return (socket_t *)socket;
}
