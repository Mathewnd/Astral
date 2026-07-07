#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/sock.h>

syscallret_t syscall_listen(context_t *, int fd, int backlog) {
	syscallret_t ret = {
		.ret = -1
	};

	vnode_t *vnode;
	ret.errno = sockfd_get(fd, &vnode, NULL);
	if (ret.errno)
		return ret;

	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(vnode);

	ret.errno = socket->ops->listen ? socket->ops->listen(socket, backlog) : EOPNOTSUPP;
	ret.ret = ret.errno ? -1 : 0;

	VOP_RELEASE(vnode);

	return ret;
}
