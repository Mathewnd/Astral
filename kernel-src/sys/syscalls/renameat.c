#include <kernel/syscalls.h>
#include <kernel/alloc.h>
#include <string.h>
#include <logging.h>

syscallret_t syscall_renameat(context_t *context, int olddirfd, char *uoldpath, int newdirfd, char *unewpath, unsigned int flags) {
	syscallret_t ret = {
		.ret = -1
	};

	__assert(flags == 0);

	size_t oldpathlen;
	size_t newpathlen;

	if (usercopy_strlen(uoldpath, &oldpathlen) || usercopy_strlen(unewpath, &newpathlen)) {
		ret.errno = EFAULT;
		return ret;
	}

	char *oldcomponent = NULL;
	char *newcomponent = NULL;
	char *oldpath = alloc(oldpathlen + 1);
	if (oldpath == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	char *newpath = alloc(newpathlen + 1);
	if (newpath == NULL) {
		free(oldpath);
		ret.errno = ENOMEM;
		return ret;
	}

	if (usercopy_fromuser(oldpath, uoldpath, oldpathlen) || usercopy_fromuser(newpath, unewpath, newpathlen)) {
		ret.errno = EFAULT;
		return ret;
	}

	vnode_t *olddirnode = NULL;
	file_t *oldfile = NULL;
	vnode_t *newdirnode = NULL;
	file_t *newfile = NULL;
	vnode_t *targetdirnode = NULL;
	vnode_t *sourcedirnode = NULL;
	ret.errno = dirfd_enter(oldpath, olddirfd, &oldfile, &olddirnode);

	if (ret.errno)
		goto cleanup;

	ret.errno = dirfd_enter(newpath, newdirfd, &newfile, &newdirnode);

	if (ret.errno)
		goto cleanup;

	oldcomponent = alloc(strlen(oldpath) + 1);
	newcomponent = alloc(strlen(newpath) + 1);

	if (oldcomponent == NULL && newcomponent == NULL)
		goto cleanup;

	ret.errno = vfs_lookup(&sourcedirnode, olddirnode, oldpath, oldcomponent, VFS_LOOKUP_NOLINK | VFS_LOOKUP_PARENT);
	if (ret.errno)
		goto cleanup;

	ret.errno = vfs_lookup(&targetdirnode, newdirnode, newpath, newcomponent, VFS_LOOKUP_NOLINK | VFS_LOOKUP_PARENT);
	if (ret.errno)
		goto cleanup;

	ret.errno = VOP_RENAME(sourcedirnode, oldcomponent, targetdirnode, newcomponent, flags);
	ret.ret = ret.errno ? -1 : 0;

	cleanup:

	if (targetdirnode)
		VOP_RELEASE(targetdirnode);

	if (sourcedirnode)
		VOP_RELEASE(sourcedirnode);

	if (oldcomponent)
		free(oldcomponent);

	if (newcomponent)
		free(newcomponent);

	if (olddirnode)
		dirfd_leave(olddirnode, oldfile);

	if (newdirnode)
		dirfd_leave(newdirnode, newfile);

	free(oldpath);
	free(newpath);

	return ret;
}
