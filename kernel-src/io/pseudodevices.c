#include <kernel/devfs.h>
#include <errno.h>
#include <string.h>
#include <logging.h>

static int null_write(int minor, void *buffer, size_t count, uintmax_t offset, int flags, size_t *wcount) {
	*wcount = count;
	return 0;
}

static int full_write(int minor, void *buffer, size_t count, uintmax_t offset, int flags, size_t *wcount) {
	return ENOSPC;
}

static int zero_read(int minor, void *buffer, size_t count, uintmax_t offset, int flags, size_t *rcount) {
	*rcount = count;
	memset(buffer, 0, count);
	return 0;
}

static int null_read(int minor, void *buffer, size_t count, uintmax_t offset, int flags, size_t *rcount) {
	*rcount = 0;
	return 0;
}

static devops_t nullops = {
	.read = null_read,
	.write = null_write
};

static devops_t fullops = {
	.read = zero_read,
	.write = full_write
};

static devops_t zeroops = {
	.read = zero_read,
	.write = null_write
};

void pseudodevices_init() {
	__assert(devfs_register(&nullops, "null", V_TYPE_CHDEV, DEV_MAJOR_NULL, 0, 0644) == 0);
	__assert(devfs_register(&fullops, "full", V_TYPE_CHDEV, DEV_MAJOR_FULL, 0, 0644) == 0);
	__assert(devfs_register(&zeroops, "zero", V_TYPE_CHDEV, DEV_MAJOR_ZERO, 0, 0644) == 0);
}
