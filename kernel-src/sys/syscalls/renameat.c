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
	ret.errno = dirfd_enter(oldpath, olddirfd, &oldfile, &olddirnode);
	if (ret.errno)
		goto cleanup;

	ret.errno = dirfd_enter(newpath, newdirfd, &newfile, &newdirnode);
	if (ret.errno)
		goto cleanup;

	ret.errno = vfs_rename(olddirnode, oldpath, newdirnode, newpath, flags);
	ret.ret = ret.errno ? -1 : 0;

	cleanup:

	if (olddirnode)
		dirfd_leave(olddirnode, oldfile);

	if (newdirnode)
		dirfd_leave(newdirnode, newfile);

	free(oldpath);
	free(newpath);

	return ret;
}
