#include <kernel/syscalls.h>
#include <errno.h>
#include <string.h>
#include <kernel/alloc.h>
#include <kernel/sock.h>
#include <kernel/file.h>
#include <kernel/vfs.h>
#include <kernel/net.h>
#include <logging.h>

syscallret_t syscall_shutdown(context_t *, int fd, int how) {
	syscallret_t ret = {
		.ret = -1
	};

	if (how > 2) {
		ret.errno = EINVAL;
		return ret;
	}

	++how; // convert to internal meaning

	vnode_t *vnode;
	ret.errno = sockfd_get(fd, &vnode, NULL);
	if (ret.errno)
		return ret;

	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(vnode);

	ret.errno = socket->ops->shutdown(socket, how);
	ret.ret = ret.errno ? -1 : 0;

	VOP_RELEASE(vnode);
	return ret;
}
