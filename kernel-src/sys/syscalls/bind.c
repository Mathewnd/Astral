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

	vnode_t *vnode = NULL;
	ret.errno = sockfd_get(fd, &vnode, NULL);
	if (ret.errno)
		goto cleanup;

	ret.errno = usercopy_fromuser(addr, uaddr, addrlen);
	if (ret.errno)
		goto cleanup;

	sockaddr_t sockaddr;
	ret.errno = sock_convertaddress(&sockaddr, addr);
	if (ret.errno)
		goto cleanup;

	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(vnode);

	ret.errno = socket->ops->bind(socket, &sockaddr, &current_thread()->proc->cred);
	ret.ret = ret.errno ? -1 : 0;

	cleanup:
	if (vnode)
		VOP_RELEASE(vnode);
	free(addr);
	return ret;
}
