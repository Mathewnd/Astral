#include <kernel/syscalls.h>
#include <kernel/abi.h>
#include <kernel/vfs.h>
#include <arch/cpu.h>
#include <errno.h>
#include <kernel/file.h>
#include <kernel/alloc.h>

typedef uint64_t fsblkcnt_t;
typedef uint64_t fsfilcnt_t;

typedef struct {
	unsigned long f_bsize;
	unsigned long f_frsize;
	fsblkcnt_t f_blocks;
	fsblkcnt_t f_bfree;
	fsblkcnt_t f_bavail;

	fsfilcnt_t f_files;
	fsfilcnt_t f_ffree;
	fsfilcnt_t f_favail;

	unsigned long f_fsid;
	unsigned long f_flag;
	unsigned long f_namemax;
} statvfs_t;

syscallret_t syscall_fstatvfs(context_t *ctx, int fd, statvfs_t *ustatvfs) {
	syscallret_t ret = {
		.ret = -1
	};

	file_t *file = fd_get(fd);
	if (file == NULL) {
		ret.errno = EBADF;
		return ret;
	}

	fsattr_t buf;
	ret.errno = VFS_STATFS(file->vnode->vfs, &buf);
	if (ret.errno)
		goto cleanup;

	statvfs_t output = {
		.f_bsize = buf.io_size,
		.f_frsize = buf.block_size,
		.f_blocks = buf.block_count,
		.f_bfree = buf.free_blocks,
		.f_bavail = buf.free_blocks_unprivileged,
		.f_files = buf.inode_count,
		.f_ffree = buf.free_inode_count,
		.f_favail = buf.free_inode_count_unprivileged,
		.f_fsid = buf.fsid,
		.f_flag = buf.flags,
		.f_namemax = buf.max_name_size
	};

	ret.errno = usercopy_touser(ustatvfs, &output, sizeof(statvfs_t));
	ret.ret = ret.errno ? -1 : 0;

	cleanup:
	fd_release(file);
	return ret;
}

syscallret_t syscall_fstatvfsat(context_t *ctx, int dirfd, char *upath, stat_t *ustatvfs, int flags) {
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
	ret.errno = dirfd_enter(path, dirfd, &file, &dirnode);
	if (ret.errno)
		goto cleanup;

	vnode_t *node = NULL;
	ret.errno = vfs_lookup(&node, dirnode, path, NULL, flags & AT_SYMLINK_NOFOLLOW ? VFS_LOOKUP_NOLINK : 0);
	if (ret.errno)
		goto cleanup;
	// locked by vfs_lookup
	VOP_UNLOCK(node);

	fsattr_t buf;
	ret.errno = VFS_STATFS(node->vfs, &buf);
	if (ret.errno)
		goto cleanup;

	statvfs_t output = {
		.f_bsize = buf.io_size,
		.f_frsize = buf.block_size,
		.f_blocks = buf.block_count,
		.f_bfree = buf.free_blocks,
		.f_bavail = buf.free_blocks_unprivileged,
		.f_files = buf.inode_count,
		.f_ffree = buf.free_inode_count,
		.f_favail = buf.free_inode_count_unprivileged,
		.f_fsid = buf.fsid,
		.f_flag = buf.flags,
		.f_namemax = buf.max_name_size
	};

	ret.errno = usercopy_touser(ustatvfs, &output, sizeof(statvfs_t));
	ret.ret = ret.errno ? -1 : 0;

	cleanup:
	if (node)
		VOP_RELEASE(node);

	if (dirnode)
		dirfd_leave(dirnode, file);

	free(path);

	return ret;
}
