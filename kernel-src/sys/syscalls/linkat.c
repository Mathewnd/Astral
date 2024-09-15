#include <kernel/syscalls.h>
#include <kernel/alloc.h>


syscallret_t syscall_linkat(context_t *, int olddirfd, char *uoldpath, int newdirfd, char *unewpath, int flags, int type) {
	syscallret_t ret = {
		.ret = -1
	};

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
		free(newpath);
		free(oldpath);
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

	vattr_t attr = {
		.gid = current_thread()->proc->cred.gid,
		.uid = current_thread()->proc->cred.uid,
		.mode = UMASK(0777)
	};

	ret.errno = vfs_link(olddirnode, oldpath, newdirnode, newpath, type == 0 ? V_TYPE_REGULAR : V_TYPE_LINK, &attr);
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
