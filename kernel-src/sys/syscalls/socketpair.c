#include <kernel/syscalls.h>
#include <kernel/sock.h>

syscallret_t syscall_socketpair(context_t *, int domain, int type, int protocol) {
	syscallret_t ret = {
		.ret = -1
	};

	// TODO flags could be OR in with type
	// we will only support pairs of local sockets
	if (domain != AF_LOCAL || type != SOCK_STREAM) {
		ret.errno = EOPNOTSUPP;
		return ret;
	}

	vnode_t *node1 = NULL;
	vnode_t *node2 = NULL;
	int fd1, fd2;
	file_t *file1 = NULL;
	file_t *file2 = NULL;
	socket_t *sock1 = NULL;
	socket_t *sock2 = NULL;

	// create pair
	ret.errno = localsock_pair(&sock1, &sock2);
	if (ret.errno)
		return ret;

	// create vnodes
	ret.errno = sockfs_newsocket(&node1, sock1);
	if (ret.errno)
		goto error;

	ret.errno = sockfs_newsocket(&node2, sock2);
	if (ret.errno)
		goto error;

	// create fds
	ret.errno = fd_new(0, &file1, &fd1);
	if (ret.errno)
		goto error;

	file1->vnode = node1;
	file1->flags = FILE_WRITE | FILE_READ;
	file1->offset = 0;
	file1->mode = 0777;

	ret.errno = fd_new(0, &file2, &fd2);
	if (ret.errno)
		goto error;

	file2->vnode = node2;
	file2->flags = FILE_WRITE | FILE_READ;
	file2->offset = 0;
	file2->mode = 0777;

	ret.errno = 0;
	ret.ret = (uint64_t)fd1 | ((uint64_t)fd2 << 32);

	return ret;
	error:

	if (file1) {
		fd_release(file1);
		fd_close(fd1);
	} else if (node1) {
		VOP_RELEASE(node1);
	} else {
		sock1->ops->destroy(sock1);
	}
	
	if (file2) {
		fd_release(file2);
		fd_close(fd2);
	} if (node2) {
		VOP_RELEASE(node2);
	} else {
		sock1->ops->destroy(sock2);
	}

	return ret;
}
