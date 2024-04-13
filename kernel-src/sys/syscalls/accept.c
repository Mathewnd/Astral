#include <kernel/syscalls.h>
#include <kernel/abi.h>
#include <kernel/sock.h>
#include <errno.h>

syscallret_t syscall_accept(context_t *, int oldfd, abisockaddr_t *abisockaddr, size_t *addrlen, int acceptflags) {
	syscallret_t ret = {
		.ret = -1
	};

	file_t *oldfile = fd_get(oldfd);
	if (oldfile == NULL) {
		ret.errno = EBADF;
		return ret;
	}

	if (oldfile->vnode->type != V_TYPE_SOCKET) {
		ret.errno = ENOTSOCK;
		goto cleanup;
	}

	socket_t *server = SOCKFS_SOCKET_FROM_NODE(oldfile->vnode);

	socket_t *client = socket_create(server->type);
	if (client == NULL) {
		ret.errno = ENOMEM;
		goto cleanup;
	}

	vnode_t *vnode;
	ret.errno = sockfs_newsocket(&vnode, client);
	if (ret.errno) {
		client->ops->destroy(client);
		goto cleanup;
	}

	sockaddr_t addr;
	ret.errno = server->ops->accept(server, client, &addr, fileflagstovnodeflags(oldfile->flags));
	if (ret.errno) {
		// socket gets deleted by node cleanup
		VOP_RELEASE(vnode);
		goto cleanup;
	}

	file_t *newfile;
	int newfd;
	
	ret.errno = fd_new(0, &newfile, &newfd);
	if (ret.errno) {
		// socket gets deleted by node cleanup
		VOP_RELEASE(vnode);
		goto cleanup;
	}

	newfile->vnode = vnode;
	newfile->flags = FILE_READ | FILE_WRITE | (acceptflags & (O_CLOEXEC | O_NONBLOCK));
	newfile->offset = 0;
	newfile->mode = 0777;

	ret.ret = newfd;

	cleanup:
	fd_release(oldfile);

	return ret;
}
