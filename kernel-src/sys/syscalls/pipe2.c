#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/vfs.h>
#include <arch/cpu.h>
#include <kernel/pipefs.h>
#include <logging.h>
#include <errno.h>

static void open(file_t *file, vnode_t *node, int flags) {
	file->vnode = node;
	file->flags = flags;
	file->offset = 0;
	file->mode = 0777;
	VOP_LOCK(node);
	VOP_OPEN(&node, fileflagstovnodeflags(flags) | V_FFLAGS_NONBLOCKING, &_cpu()->thread->proc->cred);
	VOP_UNLOCK(node);
}

syscallret_t syscall_pipe2(context_t *, int flags) {
	__assert((flags & ~(O_CLOEXEC | O_NONBLOCK)) == 0);
	syscallret_t ret = {
		.ret = -1
	};

	vnode_t *node = NULL;
	int readfd, writefd;
	file_t *readfile = NULL;
	file_t *writefile = NULL;

	ret.errno = pipefs_newpipe(&node);
	if (ret.errno)
		goto error;

	// node held again because two files will point to the same node
	VOP_HOLD(node);

	ret.errno = fd_new(flags & O_CLOEXEC, &readfile, &readfd);
	if (ret.errno)
		goto error;

	open(readfile, node, FILE_READ | (flags & O_NONBLOCK));

	ret.errno = fd_new(flags & O_CLOEXEC, &writefile, &writefd);
	if (ret.errno)
		goto error;

	open(writefile, node, FILE_WRITE | (flags & O_NONBLOCK));

	ret.errno = 0;
	ret.ret = (uint64_t)readfd | ((uint64_t)writefd << 32);

	return ret;

	error:

	if (readfile) {
		fd_release(readfile);
		fd_close(readfd);
	}

	if (writefile) {
		fd_release(writefile);
		fd_close(writefd);
	}

	if (node) {
		// released twice because of two holds (one at pipefs_newpipe and the manual hold)
		VOP_RELEASE(node);
		VOP_RELEASE(node);
	}

	return ret;
}
