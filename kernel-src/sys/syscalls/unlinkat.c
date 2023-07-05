#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/alloc.h>
#include <kernel/vfs.h>
#include <arch/cpu.h>
#include <string.h>

#define AT_SYMLINK_NOFOLLOW 0x100

syscallret_t syscall_unlinkat(context_t *, int dirfd, const char *upath, int flags) {
	syscallret_t ret = {
		.ret = -1
	};

	size_t pathlen = strlen(upath) + 1;
	char *path = alloc(pathlen);
	if (path == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	// TODO safe strcopy
	strcpy(path, upath);

	vnode_t *dirnode = NULL;
	file_t *file = NULL;

	char *component = alloc(pathlen);
	if (component == NULL) {
		ret.errno = ENOMEM;
		goto cleanup;
	}

	if (*path == '/') {
		dirnode = sched_getroot();
	} else if (dirfd == AT_FDCWD) {
		dirnode = sched_getcwd();
	} else {
		file = fd_get(dirfd);
		if (file == NULL) {
			ret.errno = EBADF;
			goto cleanup;
		}

		dirnode = file->vnode;

		if (dirnode->type != V_TYPE_DIR) {
			ret.errno = ENOTDIR;
			goto cleanup;
		}
	}

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

	if (file)
		fd_release(file);
	else
		VOP_RELEASE(dirnode);

	free(path);

	return ret;
}
