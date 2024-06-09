#include <kernel/syscalls.h>
#include <kernel/vmm.h>
#include <kernel/alloc.h>
#include <kernel/vfs.h>
#include <logging.h>

static void freeptrs(void *a, void *b, void *c) {
	if (a)
		free(a);

	if (b)
		free (b);

	if (c)
		free (c);
}

syscallret_t syscall_mount(context_t *context, char *ubacking, char *umountpoint, char *ufs, unsigned long flags, void *data) {
	syscallret_t ret = {
		.ret = -1
	};
	__assert(flags == 0);

	size_t mountpointlen, fslen;

	if (usercopy_strlen(umountpoint, &mountpointlen) || usercopy_strlen(ufs, &fslen)) {
		ret.errno = EFAULT;
		return ret;
	}

	ret.errno = ENOMEM;
	char *mountpoint = alloc(mountpointlen + 1);
	if (mountpoint == NULL)
		return ret;

	char *fs = alloc(fslen + 1);
	if (fs == NULL) {
		free(mountpoint);
		return ret;
	}

	char *backing = NULL;
	if (ubacking) {
		size_t backinglen;

		ret.errno = usercopy_strlen(ubacking, &backinglen);
		if (ret.errno) {
			freeptrs(mountpoint, fs, NULL);
			return ret;
		}

		backing = alloc(backinglen + 1);
		if (backing == NULL) {
			freeptrs(mountpoint, fs, NULL);
			return ret;
		}

		ret.errno = usercopy_fromuser(backing, ubacking, backinglen);
		if (ret.errno) {
			freeptrs(backing, mountpoint, fs);
			return ret;
		}
	}

	if (usercopy_fromuser(fs, ufs, fslen) || usercopy_fromuser(mountpoint, umountpoint, mountpointlen)) {
		ret.errno = EFAULT;
		freeptrs(fs, mountpoint, backing);
		return ret;
	}

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
