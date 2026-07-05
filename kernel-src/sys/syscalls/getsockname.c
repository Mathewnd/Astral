#include <kernel/syscalls.h>
#include <kernel/sock.h>

syscallret_t syscall_getsockname(context_t *, int fd, void *uaddr, int *uaddrlen) {
	syscallret_t ret = {
		.ret = -1
	};

	int addrlen;
	ret.errno = usercopy_fromuser(&addrlen, uaddrlen, sizeof(addrlen));
	if (ret.errno)
		return ret;

	file_t *file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		goto cleanup;
	}

	if (file->vnode->type != V_TYPE_SOCKET) {
		ret.errno = ENOTSOCK;
		goto cleanup;
	}

	sockaddr_t sockaddr;
	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(file->vnode);

	ret.errno = socket->ops->getname(socket, &sockaddr);
	if (ret.errno)
		goto cleanup;

	printf("namelen: %lu\n", strlen(sockaddr.path));

	socklen_t actual_len;
	ret.errno = sock_copy_addr_to_user(socket->type, &sockaddr, uaddr, addrlen, &actual_len);
	if (ret.errno)
		goto cleanup;

	addrlen = actual_len;
	ret.errno = usercopy_touser(uaddrlen, &addrlen, sizeof(addrlen));
	if (ret.errno)
		goto cleanup;

	ret.ret = 0;

	cleanup:
	if (file)
		fd_release(file);

	return ret;
}
