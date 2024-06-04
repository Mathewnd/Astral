#include <kernel/syscalls.h>
#include <kernel/net.h>
#include <kernel/abi.h>
#include <kernel/vfs.h>
#include <kernel/sock.h>
#include <kernel/file.h>
#include <kernel/alloc.h>

#define KNOWN_FLAGS (MSG_PEEK)

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
	size_t recvcount;
	ret.errno = socket->ops->recv(socket, &sockaddr, buffer, buffersize, fileflagstovnodeflags(file->flags) | ((flags & MSG_PEEK) ? SOCKET_RECV_FLAGS_PEEK : 0), &recvcount);
	ret.ret = ret.errno ? -1 : recvcount;
	if (ret.errno)
		goto cleanup;

	uintmax_t iovoffset = 0;
	for (int i = 0; i < msghdr.iovcount; ++i) {
		memcpy(msghdr.iov[i].addr, (void *)((uintptr_t)buffer + iovoffset), msghdr.iov[i].len);
		iovoffset += msghdr.iov[i].len;
	}

	if (umsghdr->addr) {
		ret.errno = sock_addrtoabiaddr(socket->type, &sockaddr, umsghdr->addr);
		if (ret.errno)
			goto cleanup;
	}

	cleanup:
	if (file)
		fd_release(file);

	if (buffer)
		vmm_unmap(buffer, buffersize, 0);

	sock_freemsghdr(&msghdr);
	return ret;
}
