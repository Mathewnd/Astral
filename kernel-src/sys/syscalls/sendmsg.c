#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <kernel/sock.h>
#include <kernel/file.h>
#include <kernel/net.h>
#include <kernel/alloc.h>

syscallret_t syscall_sendmsg(context_t *, int fd, msghdr_t *umsghdr, int flags)  {
	__assert((flags & ~MSG_NOSIGNAL) == 0);
	syscallret_t ret;

	msghdr_t msghdr;
	ret.errno = sock_copymsghdr(&msghdr, umsghdr);
	if (ret.errno)
		return ret;

	file_t *file = NULL;
	size_t buffersize = iovec_size(msghdr.iov, msghdr.iovcount);
	if (buffersize == 0) {
		sock_freemsghdr(&msghdr);
		ret.errno = 0;
		ret.ret = 0;
		return ret;
	}

	if (iovec_user_check(msghdr.iov, msghdr.iovcount) == false) {
		ret.errno = EFAULT;
		sock_freemsghdr(&msghdr);
		return ret;
	}

	file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		goto cleanup; 
	}

	if (file->vnode->type != V_TYPE_SOCKET) {
		ret.errno = ENOTSOCK;
		goto cleanup;
	}

	sockaddr_t sockaddr;
	if (msghdr.addr) {
		ret.errno = sock_convertaddress(&sockaddr, msghdr.addr);
		if (ret.errno)
			goto cleanup;
	}

	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(file->vnode);

	sockdesc_t desc = {
		.addr = msghdr.addr ? &sockaddr : NULL,
		.count = buffersize,
		.flags = fileflagstovnodeflags(file->flags) | ((flags & MSG_NOSIGNAL) ? SOCKET_SEND_FLAGS_NOSIGNAL : 0),
		.donecount = 0,
		.ctrl = msghdr.msgctrl,
		.ctrllen = msghdr.ctrllen,
		.ctrldone = 0
	};

	iovec_iterator_init(&desc.iovec_iterator, msghdr.iov, msghdr.iovcount);

	ret.errno = socket->ops->send(socket, &desc);
	ret.ret = ret.errno ? -1 : desc.donecount;

	cleanup:
	if (file)
		fd_release(file);

	sock_freemsghdr(&msghdr);
	return ret;
}
