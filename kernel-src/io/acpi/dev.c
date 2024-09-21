#include <kernel/acpi.h>
#include <kernel/devfs.h>
#include <kernel/poll.h>
#include <spinlock.h>
#include <ringbuffer.h>
#include <logging.h>

static ringbuffer_t ringbuffer;
static pollheader_t pollheader;
static mutex_t mutex;

void acpi_signaldevice(char c) {
	if (ringbuffer_write(&ringbuffer, &c, 1))
		poll_event(&pollheader, POLLIN);
}

static int internalpoll(polldata_t *data, int events) {
	int revents = 0;
	if (events & POLLOUT)
		revents |= POLLOUT;

	if ((events & POLLIN) && RINGBUFFER_DATACOUNT(&ringbuffer))
		revents |= POLLIN;

	if (revents == 0)
		poll_add(&pollheader, data, events);

	return revents;
}

static int read(int minor, iovec_iterator_t *iovec_iterator, size_t size, uintmax_t offset, int flags, size_t *readc) {
	*readc = 0;

	if (size < 1)
		return 0;

	polldesc_t desc;
	int error = poll_initdesc(&desc, 1);
	if (error)
		return error;

	MUTEX_ACQUIRE(&mutex, false);

	for (;;) {
		int revents = internalpoll(&desc.data[0], POLLIN);

		if (revents)
			break;

		if (flags & V_FFLAGS_NONBLOCKING) {
			error = EAGAIN;
			goto leave;
		}

		error = poll_dowait(&desc, 0);

		if (error)
			goto leave;

		poll_leave(&desc);
	}

	// this abuses a behavior of how the ringbuffer works to not need a lock for reading
	// (as it would need interrupts to be off)
	*readc = iovec_iterator_read_from_ringbuffer(iovec_iterator, &ringbuffer, size);
	if (*readc == RINGBUFFER_USER_COPY_FAILED)
		error = EFAULT;

	leave:
	MUTEX_RELEASE(&mutex);
	poll_leave(&desc);
	poll_destroydesc(&desc);
	return error;
}

static int write(int minor, iovec_iterator_t *iovec_iterator, size_t size, uintmax_t offset, int flags, size_t *writec) {
	if (size < 1) {
		*writec = 0;
		return 0;
	}

	char c;
	int error = iovec_iterator_copy_to_buffer(iovec_iterator, &c, 1);
	if (error)
		return error;

	switch (c) {
		case 'p':
			acpi_poweroff();
			break;
		case 'r':
			acpi_reboot();
			break;
	}

	return error;
}

static int poll(int minor, polldata_t *data, int events) {
	MUTEX_ACQUIRE(&mutex, false);
	int revents = internalpoll(data, events);
	MUTEX_RELEASE(&mutex);
	return revents;
}

static devops_t acpiops = {
	.read = read,
	.write = write,
	.poll = poll
};

void acpi_initdevice(void) {
	__assert(devfs_register(&acpiops, "acpi", V_TYPE_CHDEV, DEV_MAJOR_ACPI, 0, 0600, NULL) == 0);
	__assert(ringbuffer_init(&ringbuffer, 64) == 0);
	POLL_INITHEADER(&pollheader);
}
