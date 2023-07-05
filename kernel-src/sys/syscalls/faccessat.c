#include <kernel/syscalls.h>
#include <kernel/file.h>
#include <kernel/alloc.h>
#include <kernel/vfs.h>
#include <arch/cpu.h>
#include <string.h>

#define AT_SYMLINK_NOFOLLOW 0x100

syscallret_t syscall_faccessat(context_t *, int dirfd, const char *upath, int mode, int flags) {
	syscallret_t ret = {
		.ret = -1
	};

	char *path = alloc(strlen(upath) + 1);
	if (path == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	// TODO safe strcopy
	strcpy(path, upath);

	vnode_t *dirnode = NULL;
	file_t *file = NULL;
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

	vnode_t *node = NULL;
	ret.errno = vfs_lookup(&node, dirnode, path, NULL, flags & AT_SYMLINK_NOFOLLOW ? VFS_LOOKUP_NOLINK : 0);
	if (ret.errno)
		goto cleanup;

	ret.errno = VOP_ACCESS(node, mode, &_cpu()->thread->proc->cred);
	ret.ret = 0;

	cleanup:
	if (node)
		VOP_RELEASE(node);

	if (file)
		fd_release(file);
	else
		VOP_RELEASE(dirnode);

	free(path);

	return ret;
}
