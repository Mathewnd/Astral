#include <kernel/pty.h>
#include <kernel/devfs.h>
#include <spinlock.h>
#include <logging.h>
#include <kernel/alloc.h>

#define PTY_BUFFER 4096
#define PTY_MAX 1024
#define PTMX_MINOR PTY_MAX
static spinlock_t listlock;
static pty_t *ptylist[PTY_MAX];

static inline pty_t *ptyget(int minor) {
	if (minor >= PTY_MAX)
		return NULL;

	return ptylist[minor];
}

static int allocateptyminor(pty_t *pty) {
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&listlock);

	int i;
	for (i = 0; i < PTY_MAX; ++i) {
		if (ptylist[i] == NULL) {
			ptylist[i] = pty;
			break;
		}
	}

	spinlock_release(&listlock);
	interrupt_set(intstatus);
	return i;
}

static void freeptyminor(int pty) {
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&listlock);

	ptylist[pty] = NULL;

	spinlock_release(&listlock);
	interrupt_set(intstatus);
}

static pty_t *allocpty() {
	pty_t *pty = alloc(sizeof(pty_t));
	if (pty == NULL)
		return NULL;

	if (ringbuffer_init(&pty->ringbuffer, PTY_BUFFER)) {
		free(pty);
		return NULL;
	}

	POLL_INITHEADER(&pty->pollheader);
	SPINLOCK_INIT(pty->lock);

	return pty;
}

static void freepty(pty_t *pty) {
	ringbuffer_destroy(&pty->ringbuffer);
	free(pty);
}

static size_t writetopty(void *_pty, char *data, size_t size) {
	pty_t *pty = _pty;
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&pty->lock);

	size_t written = ringbuffer_write(&pty->ringbuffer, data, size);
	poll_event(&pty->pollheader, POLLIN);

	spinlock_release(&pty->lock);
	interrupt_set(intstatus);

	return written;
}

static void inactivepty(void *_pty) {
	pty_t *pty = _pty;
	freeptyminor(pty->minor);
	freepty(pty);
}

static int isatty(int minor) {
	return 0;
}

static int open(int minor, vnode_t **vnode, int flags);

static int close(int minor, int flags) {
	pty_t *pty = ptyget(minor);
	if (pty == NULL)
		return ENODEV;

	// TODO do tty cleanup stuff like sending sighup
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&pty->lock);
	pty->hangup = true;
	spinlock_release(&pty->lock);
	interrupt_set(intstatus);

	// this should remove the implicit pty -> tty master vnode reference
	// the master vnode of the pty will be unheld once the close exits
	// the pty_t structure and minor number will be free once the tty calls the device inactive function,
	// in case there are still processes with the pts open
	tty_unregister(pty->tty);

	return 0;
}

static int internalpoll(pty_t *pty, polldata_t *data, int events) {
	int revents = 0;

	if (events & POLLOUT)
		revents |= POLLOUT;

	if ((events & POLLIN) && RINGBUFFER_DATACOUNT(&pty->ringbuffer))
		revents |= POLLIN;

	if (revents == 0 && data)
		poll_add(&pty->pollheader, data, events);

	return revents;
}

static int read(int minor, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc) {
	pty_t *pty = ptyget(minor);
	if (pty == NULL)
		return ENODEV;


	for (;;) {
		polldesc_t desc = {0};
		int e = poll_initdesc(&desc, 1);
		if (e)
			return e;

		bool intstatus = interrupt_set(false);
		spinlock_acquire(&pty->lock);

		int revents = internalpoll(pty, &desc.data[0], POLLIN);

		if (revents) {
			*readc = ringbuffer_read(&pty->ringbuffer, buffer, size);

			spinlock_release(&pty->lock);
			interrupt_set(intstatus);

			poll_leave(&desc);
			poll_destroydesc(&desc);
			break;
		}

		spinlock_release(&pty->lock);
		interrupt_set(intstatus);

		if (flags & V_FFLAGS_NONBLOCKING) {
			poll_leave(&desc);
			poll_destroydesc(&desc);
			return EAGAIN;
		}

		e = poll_dowait(&desc, 0);

		poll_leave(&desc);
		poll_destroydesc(&desc);

		if (e)
			return e;
	}

	return 0;
}

static int poll(int minor, polldata_t *data, int events) {
	pty_t *pty = ptyget(minor);
	if (pty == NULL)
		return POLLERR;

	bool intstatus = interrupt_set(false);
	spinlock_acquire(&pty->lock);

	int revents = internalpoll(pty, data, events);

	spinlock_release(&pty->lock);
	interrupt_set(intstatus);

	return revents;
}
#define TIOCGPTN 0x80045430
static int ioctl(int minor, unsigned long request, void *_arg, int *result) {
	pty_t *pty = ptyget(minor);
	if (pty == NULL)
		return ENODEV;

	switch (request) {
		case TIOCGPTN: {
			int *arg = _arg;
			*arg = pty->minor;
			break;
		}
		default:
		return tty_ioctl(pty->tty, request, _arg, result);
	}

	return 0;
}

static int write(int minor, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec) {
	pty_t *pty = ptyget(minor);
	if (pty == NULL)
		return ENODEV;

	char *str = buffer;

	for (int i = 0; i < size; ++i)
		tty_process(pty->tty, str[i]);

	*writec = size;
	return 0;
}

static devops_t devops = {
	.open = open,
	.isatty = isatty,
	.write = write,
	.close = close,
	.poll = poll,
	.read = read,
	.ioctl = ioctl
};

static int open(int oldminor, vnode_t **vnode, int flags) {
	if (oldminor != PTMX_MINOR)
		return EINVAL;

	pty_t *pty = allocpty();
	if (pty == NULL)
		return ENOMEM;

	int newminor = allocateptyminor(pty);
	if (newminor == PTMX_MINOR) {
		freepty(pty);
		return ENOSPC;
	}

	pty->minor = newminor;

	char tmpname[20];
	snprintf(tmpname, 20, ".ptmx%d", newminor);

	// register a master device and immediatelly remove it from the filesystem and tables
	// while holding the refcount
	int error = devfs_register(&devops, tmpname, V_TYPE_CHDEV, DEV_MAJOR_PTY, newminor, 0644);
	if (error) {
		freeptyminor(newminor);
		freepty(pty);
		return error;
	}

	__assert(devfs_getbyname(tmpname, &pty->mastervnode) == 0);

	devfs_remove(tmpname, DEV_MAJOR_PTY, newminor);

	// we are now the only ones to hold the master vnode and its inaccessible from devfs
	// create a pairing slave device

	snprintf(tmpname, 20, "pts/%d", newminor);
	pty->tty = tty_create(tmpname, writetopty, inactivepty, pty);
	if (pty->tty == NULL) {
		VOP_RELEASE(pty->mastervnode);
		return ENOMEM;
	}

	// theres an implicit pty -> tty reference to the master vnode,
	// as the pts/* link will ALWAYS exist as long as the pty has not closed

	VOP_RELEASE(*vnode);
	*vnode = pty->mastervnode;

	return 0;
}

void pty_init() {
	__assert(devfs_register(&devops, "ptmx", V_TYPE_CHDEV, DEV_MAJOR_PTY, PTMX_MINOR, 0644) == 0);
	__assert(devfs_createdir("pts") == 0);
	SPINLOCK_INIT(listlock);
}
