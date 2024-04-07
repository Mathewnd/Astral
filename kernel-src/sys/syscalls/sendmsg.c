#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <kernel/sock.h>
#include <kernel/file.h>
#include <kernel/net.h>
#include <kernel/alloc.h>

syscallret_t syscall_sendmsg(context_t *, int fd, msghdr_t *umsghdr, int flags)  {
	__assert(flags == 0);
	syscallret_t ret;

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

	uintmax_t iovoffset = 0;

	for (int i = 0; i < msghdr.iovcount; ++i) {
		memcpy((void *)((uintptr_t)buffer + iovoffset), msghdr.iov[i].addr, msghdr.iov[i].len);
		iovoffset += msghdr.iov[i].len;
	}

	file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		goto cleanup; 
	}

	sockaddr_t sockaddr;
	if (msghdr.addr) {
		ret.errno = sock_convertaddress(&sockaddr, msghdr.addr);
		if (ret.errno)
			goto cleanup;
	}

	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(file->vnode);
	size_t sendcount;
	ret.errno = socket->ops->send(socket, msghdr.addr ? &sockaddr : NULL, buffer, buffersize, 0, &sendcount);
	ret.ret = ret.errno ? -1 : sendcount;

	cleanup:
	if (file)
		fd_release(file);

	if (buffer)
		vmm_unmap(buffer, buffersize, 0);

	sock_freemsghdr(&msghdr);
	return ret;
}
