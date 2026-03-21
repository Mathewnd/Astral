#ifndef _SYSCALLS_H
#define _SYSCALLS_H

#include <stdint.h>
#include <arch/context.h>
#include <kernel/vfs.h>
#include <kernel/file.h>
#include <kernel/scheduler.h>
#include <kernel/usercopy.h>

typedef struct {
	uint64_t ret;
	uint64_t errno;
} syscallret_t;

static inline int dirfd_enter(char *path, int dirfd, file_t **file, vnode_t **dirnode) {
	if (*path == '/') {
		*dirnode = proc_get_root();
	} else if (dirfd == AT_FDCWD) {
		*dirnode = proc_get_cwd();
	} else {
		file_t *f = fd_get(dirfd);
		if (f == NULL)
			return EBADF;

		if (f->vnode->type != V_TYPE_DIR) {
			fd_release(f);
			return ENOTDIR;
		}

		*file = f;
		*dirnode = f->vnode;
	}
	return 0;
}

static inline void dirfd_leave(vnode_t *dirnode, file_t *file) {
	if (file)
		fd_release(file);
	else
		VOP_RELEASE(dirnode);
}

#endif
