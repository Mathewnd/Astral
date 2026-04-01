#include <kernel/syscalls.h>
#include <kernel/sock.h>
#include <kernel/abi.h>
#include <kernel/file.h>
#include <errno.h>

syscallret_t syscall_getsockopt(context_t *, int fd, int level, int optname, void *val, socklen_t *len) {
	syscallret_t ret = {
		.ret = -1
	};

	if (!IS_USER_ADDRESS(val) || !IS_USER_ADDRESS(len)) {
		ret.errno = EFAULT;
	}

	file_t *file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		return ret;
	}

	if (file->vnode->type != V_TYPE_SOCKET) {
		ret.errno = ENOTSOCK;
		goto cleanup;
	}

	socket_t *socket = SOCKFS_SOCKET_FROM_NODE(file->vnode); 

	MUTEX_ACQUIRE(&socket->mutex);
	if (level == SOL_SOCKET) {
		// handle some general cases
		switch (optname) {
			case SO_ERROR: {
				socklen_t l;
				ret.errno = usercopy_fromuser(&l, len, sizeof(l));
				if (ret.errno)
					break;

				if (l != 4) {
					ret.errno = EINVAL;
					break;
				}

				int error = socket->error;
				ret.errno = usercopy_touser(val, &error, sizeof(error));
				if (!ret.errno)
					socket->error = 0;

				break;
			}
			default: {
				ret.errno = socket->ops->getopt ? socket->ops->getopt(socket, level, optname, val, len, &current_thread()->proc->cred) : ENOPROTOOPT;
			}
		}
	} else {
		ret.errno = socket->ops->getopt ? socket->ops->getopt(socket, level, optname, val, len, &current_thread()->proc->cred) : ENOPROTOOPT;
	}

	MUTEX_RELEASE(&socket->mutex);

	cleanup:
	ret.ret = ret.errno ? -1 : 0;

	fd_release(file);
	return ret;
}
