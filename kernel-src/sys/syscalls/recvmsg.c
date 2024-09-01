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

	void *buffer = vmm_map(NULL, buffersize, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	if (buffer == NULL) {
		ret.errno = ENOMEM;
		goto cleanup;
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
		.buffer = buffer,
		.count = buffersize,
		.flags = fileflagstovnodeflags(file->flags) | recvflags,
		.donecount = 0,
		.ctrl = msghdr.msgctrl,
		.ctrllen = msghdr.ctrllen,
		.ctrldone = 0
	};

	ret.errno = socket->ops->recv(socket, &desc);
	if (ret.errno)
		goto cleanup;

	msghdr.ctrllen = desc.ctrldone;
	msghdr.flags = (desc.flags & SOCKET_RECV_FLAGS_CTRLTRUNCATED) ? MSG_CTRUNC : 0;

	uintmax_t iovoffset = 0;
	for (int i = 0; i < msghdr.iovcount; ++i) {
		ret.errno = usercopy_touser(msghdr.iov[i].addr, (void *)((uintptr_t)buffer + iovoffset), msghdr.iov[i].len);
		if (ret.errno)
			goto cleanup;
		iovoffset += msghdr.iov[i].len;
	}

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

		ret.errno = usercopy_touser(ptr, msghdr.msgctrl, desc.ctrldone);
		if (ret.errno)
			goto cleanup;
	}

	ret.ret = ret.errno ? -1 : desc.donecount;

	cleanup:
	if (file)
		fd_release(file);

	if (buffer)
		vmm_unmap(buffer, buffersize, 0);

	sock_freemsghdr(&msghdr);
	return ret;
}
