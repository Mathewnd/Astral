#include <kernel/acpi.h>
#include <kernel/devfs.h>
#include <kernel/poll.h>
#include <spinlock.h>
#include <ringbuffer.h>
#include <logging.h>

static ringbuffer_t ringbuffer;
static pollheader_t pollheader;
static spinlock_t lock;

void acpi_signaldevice(char c) {
	long ipl = spinlock_acquireraiseipl(&lock, IPL_DPC);

	if (ringbuffer_write(&ringbuffer, &c, 1))
		poll_event(&pollheader, POLLIN);

	spinlock_releaseloweripl(&lock, ipl);
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

static int read(int minor, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc) {
	*readc = 0;

	if (size < 1)
		return 0;

	polldesc_t desc;
	int error = poll_initdesc(&desc, 1);
	if (error)
		return error;

	for (;;) {
		long ipl = spinlock_acquireraiseipl(&lock, IPL_DPC);

		int revents = internalpoll(&desc.data[0], POLLIN);

		spinlock_releaseloweripl(&lock, ipl);

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

	long ipl = spinlock_acquireraiseipl(&lock, IPL_DPC);

	*readc = ringbuffer_read(&ringbuffer, buffer, size);

	spinlock_releaseloweripl(&lock, ipl);

	leave:
	poll_leave(&desc);
	poll_destroydesc(&desc);
	return error;
}

static int write(int minor, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec) {
	if (size < 1) {
		*writec = 0;
		return 0;
	}

	char *c = buffer;
	switch (*c) {
		case 'p':
			acpi_poweroff();
			break;
		case 'r':
			acpi_reboot();
			break;
	}

	return EIO;
}

static int poll(int minor, polldata_t *data, int events) {
	long ipl = spinlock_acquireraiseipl(&lock, IPL_DPC);

	int revents = internalpoll(data, events);

	spinlock_releaseloweripl(&lock, ipl);
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
