#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <kernel/file.h>
#include <kernel/alloc.h>
#include <arch/cpu.h>

syscallret_t syscall_readlinkat(context_t *, int dirfd, const char *upath, char *ubuffer, size_t ubuffersize) {
	syscallret_t ret = {
		.ret = -1
	};

	char *path = alloc(strlen(upath) + 1);
	if (path == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	strcpy(path, upath);

	file_t *file = NULL;
	vnode_t *dirnode = NULL;
	char *buffer = NULL;
	ret.errno = dirfd_enter(path, dirfd, &file, &dirnode);
	if (ret.errno)
		goto cleanup;

	vnode_t *node = NULL;
	ret.errno = vfs_lookup(&node, dirnode, path, NULL, VFS_LOOKUP_NOLINK);
	if (ret.errno)
		goto cleanup;

	printf("buffer: %p\n", buffer);

	ret.errno = VOP_READLINK(node, &buffer, &_cpu()->thread->proc->cred);
	if (ret.errno)
		goto cleanup;

	size_t bufferlen = strlen(buffer);
	size_t copylen = bufferlen > ubuffersize ? ubuffersize : bufferlen;

	memcpy(ubuffer, buffer, copylen);

	ret.ret = copylen;

	cleanup:
	printf("buffer (cl): %p\n", buffer);

	if (buffer)
		free(buffer);

	if (node)
		VOP_RELEASE(node);

	if (dirnode)
		dirfd_leave(dirnode, file);

	return ret;
}
