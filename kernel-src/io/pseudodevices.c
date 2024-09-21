#include <kernel/devfs.h>
#include <errno.h>
#include <string.h>
#include <logging.h>
#include <kernel/timekeeper.h>

static int null_write(int minor, iovec_iterator_t *iovec_iterator, size_t count, uintmax_t offset, int flags, size_t *wcount) {
	*wcount = count;
	return 0;
}

static int full_write(int minor, iovec_iterator_t *iovec_iterator, size_t count, uintmax_t offset, int flags, size_t *wcount) {
	return ENOSPC;
}

static int zero_read(int minor, iovec_iterator_t *iovec_iterator, size_t count, uintmax_t offset, int flags, size_t *rcount) {
	*rcount = count;
	return iovec_iterator_memset(iovec_iterator, 0, count);
}

static int null_read(int minor, iovec_iterator_t *iovec_iterator, size_t count, uintmax_t offset, int flags, size_t *rcount) {
	*rcount = 0;
	return 0;
}

static int maxseek(int minor, size_t *max) {
	*max = 0;
	return 0;
}

static long current = 0xdeadbeefbadc0ffe;

static uint8_t getrand8() {
	current += timekeeper_timefromboot().ns * timekeeper_timefromboot().s - timekeeper_time().ns * timekeeper_time().s + timekeeper_time().s;
	return current & 0xff;
}

static int urandom_read(int minor, iovec_iterator_t *iovec_iterator, size_t count, uintmax_t offset, int flags, size_t *rcount) {
	for (int i = 0; i < count; ++i) {
		uint8_t byte = getrand8();
		int error = iovec_iterator_copy_from_buffer(iovec_iterator, &byte, sizeof(byte));
		if (error)
			return error;
	}

	*rcount = count;
	return 0;
}

static devops_t nullops = {
	.read = null_read,
	.write = null_write,
	.maxseek = maxseek
};

static devops_t fullops = {
	.read = zero_read,
	.write = full_write,
	.maxseek = maxseek
};

static devops_t zeroops = {
	.read = zero_read,
	.write = null_write,
	.maxseek = maxseek
};

static devops_t urandomops = {
	.read = urandom_read,
	.write = null_write,
	.maxseek = maxseek
};

void pseudodevices_init() {
	__assert(devfs_register(&nullops, "null", V_TYPE_CHDEV, DEV_MAJOR_NULL, 0, 0666, NULL) == 0);
	__assert(devfs_register(&fullops, "full", V_TYPE_CHDEV, DEV_MAJOR_FULL, 0, 0666, NULL) == 0);
	__assert(devfs_register(&zeroops, "zero", V_TYPE_CHDEV, DEV_MAJOR_ZERO, 0, 0666, NULL) == 0);
	__assert(devfs_register(&urandomops, "urandom", V_TYPE_CHDEV, DEV_MAJOR_URANDOM, 0, 0666, NULL) == 0);
}
