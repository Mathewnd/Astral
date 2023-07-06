#include <kernel/syscalls.h>
#include <kernel/alloc.h>


syscallret_t syscall_linkat(context_t *, int olddirfd, const char *uoldpath, int newdirfd, const char *unewpath, int flags, int type) {
	syscallret_t ret = {
		.ret = -1
	};

	char *oldpath = alloc(strlen(uoldpath) + 1);
	if (oldpath == NULL) {
		ret.errno = ENOMEM;
		return ret;
	}

	char *newpath = alloc(strlen(unewpath) + 1);
	if (newpath == NULL) {
		free(oldpath);
		ret.errno = ENOMEM;
		return ret;
	}

	// TODO safe strcopy
	strcpy(oldpath, uoldpath);
	strcpy(newpath, unewpath);

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
		.gid = _cpu()->thread->proc->cred.gid,
		.uid = _cpu()->thread->proc->cred.uid
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
