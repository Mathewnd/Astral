#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <kernel/file.h>
#include <kernel/alloc.h>
#include <arch/cpu.h>

syscallret_t syscall_readlinkat(context_t *, int dirfd, char *upath, char *ubuffer, size_t ubuffersize) {
	syscallret_t ret = {
		.ret = -1
	};

	size_t pathlen;
	ret.errno = usercopy_strlen(upath, &pathlen);
	if (ret.errno)
		return ret;

	char *path = alloc(pathlen + 1);
	if (path == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	ret.errno = usercopy_fromuser(path, upath, pathlen);
	if (ret.errno) {
		free(path);
		return ret;
	}

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

	ret.errno = VOP_READLINK(node, &buffer, &current_thread()->proc->cred);
	// locked by vfs_lookup
	VOP_UNLOCK(node);
	if (ret.errno)
		goto cleanup;

	size_t bufferlen = strlen(buffer);
	size_t copylen = bufferlen > ubuffersize ? ubuffersize : bufferlen;

	ret.errno = usercopy_touser(ubuffer, buffer, copylen);
	ret.ret = ret.errno ? -1 : copylen;

	cleanup:

	if (buffer)
		free(buffer);

	if (node)
		VOP_RELEASE(node);

	if (dirnode)
		dirfd_leave(dirnode, file);

	return ret;
}
