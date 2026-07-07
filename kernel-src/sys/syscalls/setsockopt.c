#include <kernel/syscalls.h>
#include <kernel/sock.h>
#include <kernel/abi.h>
#include <kernel/file.h>
#include <kernel/usercopy.h>
#include <errno.h>

syscallret_t syscall_setsockopt(context_t *, int fd, int level, int optname, void *val, size_t len) {
	syscallret_t ret = {
		.ret = -1
	};

	vnode_t *vnode;
	ret.errno = sockfd_get(fd, &vnode, NULL);
	if (ret.errno)
		return ret;

	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(vnode);

	MUTEX_ACQUIRE(&socket->mutex);
	if (level == SOL_SOCKET) {
		// handle some generic cases here
		switch (optname) {
			case SO_BINDTODEVICE: {
				if (val) {
					size_t size;
					ret.errno = usercopy_strlen(val, &size);
					if (ret.errno)
						break;

					if (size > 32) {
						ret.errno = EINVAL;
						break;
					}

					char name[size + 1];
					ret.errno = usercopy_fromuser(name, val, size);
					if (ret.errno)
						break;
					name[size] = '\0';

					socket->netdev = netdev_getdev(name);
					ret.errno = socket->netdev ? 0 : ENODEV;
				} else {
					socket->netdev = NULL;
				}
				break;
			}
			case SO_BROADCAST: {
				ret.errno = usercopy_fromuser(&socket->broadcast, val, sizeof(int));
				break;
			}
			default:
				ret.errno = socket->ops->setopt ? socket->ops->setopt(socket, level, optname, val, len, &current_thread()->proc->cred) : ENOPROTOOPT;
		}
	} else {
		ret.errno = socket->ops->setopt ? socket->ops->setopt(socket, level, optname, val, len, &current_thread()->proc->cred) : ENOPROTOOPT;
	}

	MUTEX_RELEASE(&socket->mutex);

	ret.ret = ret.errno ? -1 : 0;

	VOP_RELEASE(vnode);

	return ret;
}
