#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/sock.h>

syscallret_t syscall_listen(context_t *, int fd, int backlog) {
	syscallret_t ret = {
		.ret = -1
	};

	file_t *file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		return ret;
	}

	if (file->vnode->type != V_TYPE_SOCKET) {
		ret.errno = ENOTSOCK;
		goto cleanup;
	}

	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(file->vnode);

	ret.errno = socket->ops->listen ? socket->ops->listen(socket, backlog) : EOPNOTSUPP;
	ret.ret = ret.errno ? -1 : 0;

	cleanup:
	fd_release(file);

	return ret;
}
