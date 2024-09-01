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

	void *buffer = vmm_map(NULL, buffersize, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	if (buffer == NULL) {
		ret.errno = ENOMEM;
		goto cleanup;
	}

	uintmax_t iovoffset = 0;

	for (int i = 0; i < msghdr.iovcount; ++i) {
		ret.errno = usercopy_fromuser((void *)((uintptr_t)buffer + iovoffset), msghdr.iov[i].addr, msghdr.iov[i].len);
		if (ret.errno)
			goto cleanup;

		iovoffset += msghdr.iov[i].len;
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
		.buffer = buffer,
		.count = buffersize,
		.flags = fileflagstovnodeflags(file->flags) | ((flags & MSG_NOSIGNAL) ? SOCKET_SEND_FLAGS_NOSIGNAL : 0),
		.donecount = 0,
		.ctrl = msghdr.msgctrl,
		.ctrllen = msghdr.ctrllen,
		.ctrldone = 0
	};

	ret.errno = socket->ops->send(socket, &desc);
	ret.ret = ret.errno ? -1 : desc.donecount;

	cleanup:
	if (file)
		fd_release(file);

	if (buffer)
		vmm_unmap(buffer, buffersize, 0);

	sock_freemsghdr(&msghdr);
	return ret;
}
