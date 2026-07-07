#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <kernel/sock.h>
#include <kernel/file.h>
#include <kernel/net.h>
#include <kernel/alloc.h>

syscallret_t syscall_sendmsg(context_t *, int fd, msghdr_t *umsghdr, int flags)  {
	syscallret_t ret = {
		.ret = -1
	};

	if (flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL))
		printf("sendmsg: unknown %x\n", flags);

	msghdr_t msghdr;
	ret.errno = sock_copymsghdr(&msghdr, umsghdr);
	if (ret.errno)
		return ret;

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

	vnode_t *vnode = NULL;
	int fileflags;
	ret.errno = sockfd_get(fd, &vnode, &fileflags);
	if (ret.errno)
		goto cleanup;

	sockaddr_t sockaddr;
	if (msghdr.addr) {
		ret.errno = sock_convertaddress(&sockaddr, msghdr.addr);
		if (ret.errno)
			goto cleanup;
	}

	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(vnode);

	int sendflags = 0;

	if (flags & MSG_NOSIGNAL)
		sendflags |= SOCKET_SEND_FLAGS_NOSIGNAL;

	if (flags & MSG_DONTWAIT)
		sendflags |= V_FFLAGS_NONBLOCKING;

	iovec_iterator_t iovec_iterator;
	sockdesc_t desc = {
		.addr = msghdr.addr ? &sockaddr : NULL,
		.iovec_iterator = &iovec_iterator,
		.count = buffersize,
		.flags = fileflagstovnodeflags(fileflags) | sendflags,
		.donecount = 0,
		.ctrl = msghdr.msgctrl,
		.ctrllen = msghdr.ctrllen,
		.ctrldone = 0
	};

	iovec_iterator_init(desc.iovec_iterator, msghdr.iov, msghdr.iovcount);

	ret.errno = socket->ops->send(socket, &desc);
	ret.ret = ret.errno ? -1 : desc.donecount;

	cleanup:
	if (vnode)
		VOP_RELEASE(vnode);
	sock_freemsghdr(&msghdr);
	return ret;
}
