#include <kernel/syscalls.h>
#include <kernel/vmm.h>
#include <kernel/alloc.h>
#include <kernel/vfs.h>
#include <logging.h>

syscallret_t syscall_mount(context_t *context, const char *ubacking, const char *umountpoint, const char *ufs, unsigned long flags, void *data) {
	syscallret_t ret = {
		.ret = -1
	};
	__assert(flags == 0);

	if ((void *)ubacking > USERSPACE_END || (void *)umountpoint > USERSPACE_END || (void *)ufs > USERSPACE_END) {
		ret.errno = EFAULT;
		return ret;
	}

	ret.errno = ENOMEM;
	char *mountpoint = alloc(strlen(umountpoint) + 1);
	if (mountpoint == NULL) {
		return ret;
	}

	char *fs = alloc(strlen(ufs) + 1);
	if (fs == NULL) {
		free(mountpoint);
		return ret;
	}

	char *backing = NULL;
	if (ubacking) {
		backing = alloc(strlen(ubacking) + 1);
		if (backing == NULL) {
			free(mountpoint);
			free(fs);
			return ret;
		}
		strcpy(backing, ubacking);
	}
	strcpy(fs, ufs);
	strcpy(mountpoint, umountpoint);

	vnode_t *backingrefnode = NULL;
	if (ubacking)
		backingrefnode = *backing == '/' ? sched_getroot() : sched_getcwd();
	vnode_t *mountpointrefnode = *mountpoint == '/' ? sched_getroot() : sched_getcwd();
	vnode_t *backingnode = NULL;

	if (ubacking) {
		ret.errno = vfs_lookup(&backingnode, backingrefnode, backing, NULL, 0);
		if (ret.errno)
			goto cleanup;
	}

	ret.errno = vfs_mount(backingnode, mountpointrefnode, mountpoint, fs, data);
	if (ubacking) {
		VOP_RELEASE(backingnode);
	}

	cleanup:
	if (ubacking) {
		VOP_RELEASE(backingrefnode);
		free(backing);
	}

	VOP_RELEASE(mountpointrefnode);

	free(mountpoint);
	free(fs);
	return ret;
}
