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

	++how; // convert to internal meaning
	
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

	ret.errno = socket->ops->shutdown(socket, how);
	ret.ret = ret.errno ? -1 : 0;

	cleanup:

	fd_release(file);
	return ret;
}
