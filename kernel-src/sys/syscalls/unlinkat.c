#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/alloc.h>
#include <kernel/vfs.h>
#include <arch/cpu.h>
#include <string.h>

syscallret_t syscall_unlinkat(context_t *, int dirfd, char *upath, int flags) {
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

	vnode_t *dirnode = NULL;
	file_t *file = NULL;

	char *component = alloc(pathlen + 1);
	if (component == NULL) {
		ret.errno = ENOMEM;
		goto cleanup;
	}

	ret.errno = dirfd_enter(path, dirfd, &file, &dirnode);
	if (ret.errno)
		goto cleanup;

	// TODO rmdir flag

	vnode_t *node = NULL;
	ret.errno = vfs_lookup(&node, dirnode, path, component, VFS_LOOKUP_NOLINK | VFS_LOOKUP_PARENT);
	if (ret.errno)
		goto cleanup;

	ret.errno = VOP_UNLINK(node, component, &_cpu()->thread->proc->cred);
	ret.ret = ret.errno ? -1 : 0;

	cleanup:
	if (component)
		free(component);

	if (node)
		VOP_RELEASE(node);

	if (dirnode)
		dirfd_leave(dirnode, file);

	free(path);

	return ret;
}
