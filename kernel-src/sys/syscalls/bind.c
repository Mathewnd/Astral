#include <kernel/syscalls.h>
#include <errno.h>
#include <string.h>
#include <kernel/alloc.h>
#include <kernel/sock.h>
#include <kernel/file.h>
#include <kernel/vfs.h>
#include <kernel/net.h>
#include <logging.h>

syscallret_t syscall_bind(context_t *, int fd, void *uaddr, size_t addrlen) {
	syscallret_t ret = {
		.ret = -1
	};

	if (addrlen > 1024) {
		ret.errno = EINVAL;
		return ret;
	}

	abisockaddr_t *addr = alloc(addrlen);
	if (addr == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	file_t *file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		goto cleanup;
	}

	if (file->vnode->type != V_TYPE_SOCKET) {
		ret.errno = ENOTSOCK;
		goto cleanup;
	}

	ret.errno = usercopy_fromuser(addr, uaddr, addrlen);
	if (ret.errno)
		goto cleanup;

	sockaddr_t sockaddr;
	ret.errno = sock_convertaddress(&sockaddr, addr);
	if (ret.errno)
		goto cleanup;

	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(file->vnode);

	ret.errno = socket->ops->bind(socket, &sockaddr);
	ret.ret = ret.errno ? -1 : 0;

	cleanup:
	if (file)
		fd_release(file);

	free(addr);
	return ret;
}
