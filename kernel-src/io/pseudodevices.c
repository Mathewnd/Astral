#include <kernel/devfs.h>
#include <errno.h>
#include <string.h>
#include <logging.h>
#include <kernel/timekeeper.h>

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

static long current = 0xdeadbeefbadc0ffe;

static uint8_t getrand8() {
	current += timekeeper_timefromboot().ns * timekeeper_timefromboot().s - timekeeper_time().ns * timekeeper_time().s + timekeeper_time().s;
	return current & 0xff;
}

static int urandom_read(int minor, void *_buffer, size_t count, uintmax_t offset, int flags, size_t *rcount) {
	uint8_t *buffer = _buffer;
	for (int i = 0; i < count; ++i)
		buffer[i] = getrand8();

	*rcount = count;
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

static devops_t urandomops = {
	.read = urandom_read,
	.write = null_write
};

void pseudodevices_init() {
	__assert(devfs_register(&nullops, "null", V_TYPE_CHDEV, DEV_MAJOR_NULL, 0, 0666, NULL) == 0);
	__assert(devfs_register(&fullops, "full", V_TYPE_CHDEV, DEV_MAJOR_FULL, 0, 0666, NULL) == 0);
	__assert(devfs_register(&zeroops, "zero", V_TYPE_CHDEV, DEV_MAJOR_ZERO, 0, 0666, NULL) == 0);
	__assert(devfs_register(&urandomops, "urandom", V_TYPE_CHDEV, DEV_MAJOR_URANDOM, 0, 0666, NULL) == 0);
}
