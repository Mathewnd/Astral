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
	VOP_OPEN(&node, fileflagstovnodeflags(flags), &_cpu()->thread->proc->cred);
}

syscallret_t syscall_pipe2(context_t *, int flags) {
	// TODO support them
	__assert(flags == 0);
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

	ret.errno = fd_new(0, &readfile, &readfd);
	if (ret.errno)
		goto error;

	open(readfile, node, FILE_READ);

	ret.errno = fd_new(0, &writefile, &writefd);
	if (ret.errno)
		goto error;

	open(writefile, node, FILE_WRITE);

	ret.errno = 0;
	ret.ret = (uint64_t)readfd | ((uint64_t)writefd << 32);
	printf("pipe2: readfd %d writefd %d wrefcount %d\n", readfd, writefd, writefile->refcount);

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
