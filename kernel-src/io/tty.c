#include <kernel/tty.h>
#include <kernel/devfs.h>
#include <logging.h>
#include <kernel/alloc.h>
#include <kernel/jobctl.h>
#include <kernel/usercopy.h>

#define TTY_MAX_COUNT 4096
#define CONTROLLING_TTY_MINOR TTY_MAX_COUNT
static mutex_t listmutex;
static tty_t *ttylist[TTY_MAX_COUNT];

static proc_t *getsession(tty_t *tty) {
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&tty->sessionlock);
	proc_t *session = tty->session;
	if (session) {
		PROC_HOLD(session);
	}
	spinlock_release(&tty->sessionlock);
	interrupt_set(intstatus);
	return session;
}

static proc_t *getforeground(tty_t *tty) {
	proc_t *session = getsession(tty);
	proc_t *foreground = NULL;
	if (session) {
		foreground = jobctl_getforeground(session);
		PROC_RELEASE(session);
	}
	return foreground;
}

static int setforeground(tty_t *tty, proc_t *proc) {
	proc_t *session = getsession(tty);
	if (session) {
		int error = jobctl_setforeground(session, proc);
		PROC_RELEASE(session);
		return error;
	}
	return ENOTTY;
}

void tty_process(tty_t *tty, char c) {
	if (c == '\r' && (tty->termios.c_iflag & IGNCR))
		return;

	if (c == '\r' && (tty->termios.c_iflag & ICRNL))
		c = '\n';
	else if (c == '\n' && (tty->termios.c_iflag & INLCR))
		c = '\r';

	// TODO VSTART VSTOP

	char echoc = (tty->termios.c_lflag & ECHO) ? c : '\0';

	// echo control characters
	if ((tty->termios.c_lflag & ECHOCTL) && (tty->termios.c_lflag & ECHO) && c < 32 && c != '\n' && c != '\r' && c != '\b' && c != '\t' && c != '\e') {
		char tmp[2] = {'^', c + 0x40};
		tty->writetodevice(tty->deviceinternal, tmp, 2);
		echoc = false;
	}

	if (tty->termios.c_lflag & ISIG) {
		int signal = -1;
		if (tty->termios.c_cc[VINTR] == c)
			signal = SIGINT;
		else if (tty->termios.c_cc[VQUIT] == c)
			signal = SIGQUIT;
		else if (tty->termios.c_cc[VSUSP] == c)
			signal = SIGTSTP;

		if (signal >= 0) {
			proc_t *pgrp = getforeground(tty);
			if (pgrp) {
				jobctl_signal(pgrp, signal);
				PROC_RELEASE(pgrp);
			}
			return;
		}
	}

	if (tty->termios.c_lflag & ICANON) {
		bool flush = false;
		if (c == tty->termios.c_cc[VERASE]) {
			// backspace
			if (tty->devicepos == 0)
				return;

			--tty->devicepos;
			tty->devicebuffer[tty->devicepos] = '\0';

			if (tty->termios.c_lflag & ECHO)
				tty->writetodevice(tty->deviceinternal, "\b \b", 3);
			return;
		} else if (c == tty->termios.c_cc[VKILL]) {
			// erase everything
			for (int i = 0; i < tty->devicepos + ((tty->termios.c_lflag & ECHOCTL) ? 2 : 0); ++i)
				tty->writetodevice(tty->deviceinternal, "\b \b", 3);

			tty->devicepos = 0;
			memset(tty->devicebuffer, 0, DEVICE_BUFFER_SIZE);
			return;
		} else if (c == tty->termios.c_cc[VEOF]) {
			// end of file TODO make read return 0
			flush = true;
		} else if (c == '\n' || c == tty->termios.c_cc[VEOL] || c == tty->termios.c_cc[VEOL2]) {
			// newline
			flush = true;
		}

		if (echoc)
			tty->writetodevice(tty->deviceinternal, &echoc, 1);

		// check if buffer is full
		tty->devicebuffer[tty->devicepos++] = c;
		if (tty->devicepos == DEVICE_BUFFER_SIZE)
			flush = true;

		if (flush) {
			MUTEX_ACQUIRE(&tty->readmutex, false);
			ringbuffer_write(&tty->readbuffer, tty->devicebuffer, tty->devicepos);
			tty->devicepos = 0;
			// XXX is this needed?
			size_t datacount = RINGBUFFER_DATACOUNT(&tty->readbuffer);
			if (datacount)
				poll_event(&tty->pollheader, POLLIN);

			MUTEX_RELEASE(&tty->readmutex);
		}
	} else {
		if (echoc)
			tty->writetodevice(tty->deviceinternal, &c, 1);

		MUTEX_ACQUIRE(&tty->readmutex, false);

		ringbuffer_write(&tty->readbuffer, &c, 1);
		poll_event(&tty->pollheader, POLLIN);

		MUTEX_RELEASE(&tty->readmutex);
	}
}

static inline tty_t *ttyget(int minor) {
	if (minor >= TTY_MAX_COUNT)
		return NULL;

	return ttylist[minor];
}

static int internalpoll(tty_t *tty, polldata_t *data, int events) {
	int revents = 0;

	// XXX would the device not be in the foregroup group affect this?
	if (events & POLLOUT)
		revents |= POLLOUT;

	MUTEX_ACQUIRE(&tty->readmutex, false);

	if ((events & POLLIN) && RINGBUFFER_DATACOUNT(&tty->readbuffer))
		revents |= POLLIN;

	if (revents == 0 && data)
		poll_add(&tty->pollheader, data, events);

	MUTEX_RELEASE(&tty->readmutex);

	return revents;
}

static int read(int minor, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc) {
	tty_t *tty = ttyget(minor);
	if (tty == NULL)
		return ENODEV;

	// for non canonical read
	// VMIN = 0 VTIME = 0: return immediatelly if no data in buffer
	// VMIN > 0 VTIME = 0: block until enough data has been read
	// VMIN = 0 VTIME > 0: block until either a timeout or until there is data to read
	// VMIN > 0 VTIME > 0: wait for a character indefinetly, and then keep reading more 
	// with an inter byte timeout until either the timeout expires os we have read the minimum amount of data
	// for canonical read, min and time will be VMIN 1 VTIME 0

	// XXX
	// does changing the VMIN and VTIME value from another thread change this?
	int min = (tty->termios.c_lflag & ICANON) ? 1 : tty->termios.c_cc[VMIN];
	int time = (tty->termios.c_lflag & ICANON) ? 0 : tty->termios.c_cc[VTIME];

	// TODO
	// SIGTTIN SIGTTOUT

	// read data and if theres nothing just return
	if (min == 0 && time == 0) {
		MUTEX_ACQUIRE(&tty->readmutex, false);
		*readc = ringbuffer_read(&tty->readbuffer, buffer, size);
		MUTEX_RELEASE(&tty->readmutex);
		return 0;
	}

	// time is in 1/10 of a second
	size_t timeoutus = time * 100000;
	uintmax_t spins = 0;

	int error = 0;
	size_t readcount = 0;
	size_t spaceleft = size;

	// TODO hangup stuff

	for (;;) {
		// for VMIN > 0 and VTIME > 0, the timeout is inter byte only.
		size_t effectivetimeout = (min > 0 && time > 0 && spins == 0) ? 0 : timeoutus;

		polldesc_t desc = {0};
		error = poll_initdesc(&desc, 1);
		if (error)
			goto leave;

		int revents = internalpoll(tty, &desc.data[0], POLLIN);

		if (revents) {
			// theres data to read
			MUTEX_ACQUIRE(&tty->readmutex, false);

			size_t actuallyread = ringbuffer_read(&tty->readbuffer, (void *)((uintptr_t)buffer + readcount), spaceleft);
			readcount += actuallyread;
			spaceleft -= actuallyread;
			MUTEX_RELEASE(&tty->readmutex);

			// XXX is this valid?
			if (spaceleft > 0 && readcount < min)
				continue;

			poll_leave(&desc);
			poll_destroydesc(&desc);
			break;
		}

		if (revents == 0 && flags & V_FFLAGS_NONBLOCKING) {
			// nothing to read and nonblocking
			poll_leave(&desc);
			poll_destroydesc(&desc);
			error = EAGAIN;
			goto leave;
		}

		error = poll_dowait(&desc, effectivetimeout);
		if (error == 0 && (desc.event == NULL || (desc.event && desc.event->revents == 0))) {
			// timed out!
			break;
		}

		poll_leave(&desc);
		poll_destroydesc(&desc);

		if (error)
			goto leave;
	}

	*readc = readcount;

	leave:
	return error;
}

static int write(int minor, void *_buffer, size_t size, uintmax_t offset, int flags, size_t *writec) {
	tty_t *tty = ttyget(minor);
	if (tty == NULL)
		return ENODEV;

	// TODO background process stuff etc

	char *buffer = _buffer;

	*writec = size;
	for (int i = 0; i < size; ++i) {
		if (buffer[i] == '\n' && (tty->termios.c_oflag & ONLCR)) {
			char cr = '\r';
			tty->writetodevice(tty->deviceinternal, &cr, 1);
		}
		tty->writetodevice(tty->deviceinternal, &buffer[i], 1);
	}

	return 0;
}

static int poll(int minor, polldata_t *data, int events) {
	tty_t *tty = ttyget(minor);
	if (tty == NULL)
		return POLLERR;

	return internalpoll(tty, data, events);
}

#define TIOCSCTTY 0x540E
#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define TTY_IOCTL_NAME 0x771101141113l
#define TTY_NAME_MAX 32

int tty_ioctl(tty_t *tty, unsigned long req, void *arg, int *result) {
	switch (req) {
		case TIOCGWINSZ:
			return USERCOPY_POSSIBLY_TO_USER(arg, &tty->winsize, sizeof(winsize_t));
		case TIOCSWINSZ: {
			int e = USERCOPY_POSSIBLY_FROM_USER(&tty->winsize, arg, sizeof(winsize_t));
			if (e)
				return e;

			proc_t *foreground = getforeground(tty);
			if (foreground) {
				jobctl_signal(foreground, SIGWINCH);
				PROC_RELEASE(foreground);
			}
			break;
		}
		case TCGETS:
			return USERCOPY_POSSIBLY_TO_USER(arg, &tty->termios, sizeof(termios_t));
		case TCSETS:
			return USERCOPY_POSSIBLY_FROM_USER(&tty->termios, arg, sizeof(termios_t));
		case TIOCSCTTY: {
			// set as controlling tty
			return jobctl_setctty(_cpu()->thread->proc, tty, (uintptr_t)arg == 1);
			break;
		}
		case TIOCGPGRP: {
			proc_t *foreground = getforeground(tty);
			if (foreground == NULL)
				return ENOTTY;

			int pgrp = foreground->pid;
			PROC_RELEASE(foreground);

			return USERCOPY_POSSIBLY_TO_USER(arg, &pgrp, sizeof(int));
		}
		case TIOCSPGRP: {
			int pgrp;
			int e = USERCOPY_POSSIBLY_FROM_USER(&pgrp, arg, sizeof(int));
			if (e)
				return e;

			proc_t *proc = sched_getprocfrompid(pgrp);
			if (proc == NULL)
				return ESRCH;

			int error = setforeground(tty, proc);
			PROC_RELEASE(proc);
			return error;
		}
		case TTY_IOCTL_NAME: {
			size_t len = min(strlen(tty->name) + 1, TTY_NAME_MAX);
			return USERCOPY_POSSIBLY_TO_USER(arg, tty->name, len);
		}
		default:
			return ENOTTY;
	}

	return 0;
}

static int ioctl(int minor, unsigned long req, void *arg, int *result) {
	tty_t *tty = ttyget(minor);
	if (tty == NULL)
		return ENODEV;

	return tty_ioctl(tty, req, arg, result);
}

static int isatty(int minor) {
	return 0;
}

static int open(int minor, vnode_t **vnode, int flags) {
	tty_t *tty = ttyget(minor);

	if (minor == CONTROLLING_TTY_MINOR) {
		// return controlling tty
		tty_t *ctty = jobctl_getctty(_cpu()->thread->proc);

		if (ctty == NULL) {
			return ENXIO;
		}

		VOP_RELEASE(*vnode);
		*vnode = (vnode_t *)(ctty->mastervnode);
	} else if (tty == NULL) {
		return ENODEV;
	} else if ((flags & V_FFLAGS_NOCTTY) == 0) {
		// set as controlling tty
		jobctl_setctty(_cpu()->thread->proc, tty, false);
	}

	return 0;
}

static int allocminor(tty_t *tty) {
	MUTEX_ACQUIRE(&listmutex, false);

	int i;
	for (i = 0; i < TTY_MAX_COUNT; ++i) {
		if (ttylist[i] == NULL)
			break;
	}

	ttylist[i] = tty;

	MUTEX_RELEASE(&listmutex);
	return i;
}

static void freeminor(int minor) {
	MUTEX_ACQUIRE(&listmutex, false);
	__assert(ttylist[minor]);
	ttylist[minor] = NULL;
	MUTEX_RELEASE(&listmutex);
}

static void tty_destroy(tty_t *tty) {
	freeminor(tty->minor);
	ringbuffer_destroy(&tty->readbuffer);
	free(tty->devicebuffer);
	free(tty->name);
	free(tty);
}

static void inactive(int minor) {
	tty_t *tty = ttyget(minor);
	__assert(tty);

	if (tty->inactivedevice)
		tty->inactivedevice(tty->deviceinternal);

	tty_destroy(tty);
}

static devops_t devops = {
	.write = write,
	.poll = poll,
	.read = read,
	.ioctl = ioctl,
	.isatty = isatty,
	.open = open,
	.inactive = inactive
};

tty_t *tty_create(char *name, ttydevicewritefn_t writefn, ttyinactivefn_t inactivefn, void *internal) {
	tty_t *tty = alloc(sizeof(tty_t));
	if (tty == NULL)
		return NULL;

	tty->name = alloc(strlen(name) + 1);
	if (tty->name == NULL)
		goto error;

	tty->devicebuffer = alloc(DEVICE_BUFFER_SIZE);
	if (tty->devicebuffer == NULL)
		goto error;

	if (ringbuffer_init(&tty->readbuffer, READ_BUFFER_SIZE))
		goto error;

	int minor = allocminor(tty);
	if (minor == TTY_MAX_COUNT) {
		ringbuffer_destroy(&tty->readbuffer);
		goto error;
	}

	if (devfs_register(&devops, name, V_TYPE_CHDEV, DEV_MAJOR_TTY, minor, 0600, _cpu()->thread->proc ? &_cpu()->thread->proc->cred : NULL)) {
		freeminor(minor);
		ringbuffer_destroy(&tty->readbuffer);
		goto error;
	}

	POLL_INITHEADER(&tty->pollheader);
	strcpy(tty->name, name);
	MUTEX_INIT(&tty->readmutex);
	MUTEX_INIT(&tty->writemutex);
	SPINLOCK_INIT(tty->sessionlock);

	tty->termios.c_iflag = ICRNL;
	tty->termios.c_oflag = ONLCR;
	tty->termios.c_lflag = ECHO | ICANON | ISIG | ECHOCTL;
	tty->termios.c_cflag = 0;
	tty->termios.ibaud = 38400;
	tty->termios.obaud = 38400;
	tty->termios.c_cc[VMIN] = 1;
	tty->termios.c_cc[VINTR] = 0x03;
	tty->termios.c_cc[VQUIT] = 0x1c;
	tty->termios.c_cc[VERASE] = '\b';
	tty->termios.c_cc[VKILL] = 0x15;
	tty->termios.c_cc[VEOF] = 0x04;
	tty->termios.c_cc[VSTART] = 0x11;
	tty->termios.c_cc[VSTOP] = 0x13;
	tty->termios.c_cc[VSUSP] = 0x1a;

	tty->writetodevice = writefn;
	tty->inactivedevice = inactivefn;
	tty->minor = minor;
	tty->deviceinternal = internal;

	vnode_t *newnode;
	__assert(devfs_getnode(NULL, DEV_MAJOR_TTY, minor, &newnode) == 0);

	tty->mastervnode = (vnode_t *)((devnode_t *)newnode)->master;

	VOP_RELEASE(newnode);

	return tty;
	error:
	if (tty->name)
		free(tty->name);

	if (tty->devicebuffer)
		free(tty->devicebuffer);

	free(tty);
	return NULL;
}

void tty_unregister(tty_t *tty) {
	devfs_remove(tty->name, DEV_MAJOR_TTY, tty->minor);

	proc_t *foreground = getforeground(tty);
	if (foreground) {
		jobctl_signal(foreground, SIGHUP);
		PROC_RELEASE(foreground);
	}
}


void tty_init() {
	__assert(devfs_register(&devops, "tty", V_TYPE_CHDEV, DEV_MAJOR_TTY, CONTROLLING_TTY_MINOR, 0666, NULL) == 0);
	MUTEX_INIT(&listmutex);
}
