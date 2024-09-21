#include <kernel/syscalls.h>
#include <kernel/net.h>
#include <kernel/abi.h>
#include <kernel/vfs.h>
#include <kernel/sock.h>
#include <kernel/file.h>
#include <kernel/alloc.h>

#define KNOWN_FLAGS (MSG_PEEK | MSG_WAITALL)

syscallret_t syscall_recvmsg(context_t *, int fd, msghdr_t *umsghdr, int flags) {
	__assert((flags & ~KNOWN_FLAGS) == 0);
	syscallret_t ret = {
		.ret = -1
	};

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
	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(file->vnode);

	uintmax_t recvflags = 0;
	if (flags & MSG_PEEK)
		recvflags |= SOCKET_RECV_FLAGS_PEEK;

	if (flags & MSG_WAITALL)
		recvflags |= SOCKET_RECV_FLAGS_WAITALL;

	sockdesc_t desc = {
		.addr = &sockaddr,
		.count = buffersize,
		.flags = fileflagstovnodeflags(file->flags) | recvflags,
		.donecount = 0,
		.ctrl = msghdr.msgctrl,
		.ctrllen = msghdr.ctrllen,
		.ctrldone = 0
	};

	iovec_iterator_init(&desc.iovec_iterator, msghdr.iov, msghdr.iovcount);

	ret.errno = socket->ops->recv(socket, &desc);
	if (ret.errno)
		goto cleanup;

	if (msghdr.addr) {
		abisockaddr_t abisockaddr;
		ret.errno = sock_addrtoabiaddr(socket->type, &sockaddr, &abisockaddr);
		if (ret.errno)
			goto cleanup;

		void *ptr;
		ret.errno = usercopy_fromuser(&ptr, &umsghdr->addr, sizeof(void *));
		if (ret.errno)
			goto cleanup;

		ret.errno = usercopy_touser(ptr, &abisockaddr, min(sizeof(abisockaddr), msghdr.addrlen));
		if (ret.errno)
			goto cleanup;
	}

	if (msghdr.msgctrl) {
		void *ptr;
		ret.errno = usercopy_fromuser(&ptr, &umsghdr->msgctrl, sizeof(void *));
		if (ret.errno)
			goto cleanup;

		ret.errno = usercopy_touser(&umsghdr->ctrllen, &desc.ctrldone, sizeof(desc.ctrldone));
		if (ret.errno)
			goto cleanup;

		ret.errno = usercopy_touser(ptr, msghdr.msgctrl, desc.ctrldone);
		if (ret.errno)
			goto cleanup;
	}

	if (ret.errno == 0) {
		int return_flags = (desc.flags & SOCKET_RECV_FLAGS_CTRLTRUNCATED) ? MSG_CTRUNC : 0;
		ret.errno = usercopy_touser(&umsghdr->flags, &return_flags, sizeof(return_flags));
	}

	ret.ret = ret.errno ? -1 : desc.donecount;

	cleanup:
	if (file)
		fd_release(file);

	sock_freemsghdr(&msghdr);
	return ret;
}
