#include <kernel/console.h>
#include <mutex.h>
#include <ringbuffer.h>
#include <logging.h>
#include <kernel/term.h>
#include <kernel/devfs.h>
#include <errno.h>
#include <kernel/keyboard.h>
#include <termios.h>
#include <kernel/poll.h>

#define BUFFER_SIZE 4096
static mutex_t writemutex;
static mutex_t readmutex;
static semaphore_t readsemaphore;
static ringbuffer_t readbuffer;
static thread_t *thread;
static volatile termios_t termios;
static pollheader_t pollheader;

void console_write(char *str, size_t count) {
	MUTEX_ACQUIRE(&writemutex, false);

	term_write(str, count);

	MUTEX_RELEASE(&writemutex);
}

void console_putc(char c) {
	console_write(&c, 1);
}

#define THREAD_BUFFER_SIZE 128

// taken from https://en.wikipedia.org/wiki/ANSI_escape_code#Terminal_input_sequences

#define HOME_STR "\e[1~"
#define INSERT_STR "\e[2~"
#define DELETE_STR "\e[3~"
#define END_STR "\e[4~"
#define PGUP_STR "\e[5~"
#define PGDN_STR "\e[6~"

#define UP_STR "\e[A"
#define DOWN_STR "\e[B"
#define RIGHT_STR "\e[C"
#define LEFT_STR "\e[D"

static void consolethread() {
	char buffer[THREAD_BUFFER_SIZE + 1];
	uintmax_t buffpos = 0;

	for (;;) {
		kbpacket_t packet;
		keyboard_wait(keyboard_console, &packet);
		if (packet.flags & KBPACKET_FLAGS_RELEASED)
			continue;

		char asciibuffer[2] = {0, 0};
		char *target = asciibuffer;

		if (packet.ascii) {
			if (packet.ascii == '\r' && (termios.c_iflag & IGNCR))
				continue;

			if (packet.ascii == '\r' && (termios.c_iflag & ICRNL))
				packet.ascii = '\n';
			else if (packet.ascii == '\n' && (termios.c_iflag & INLCR))
				packet.ascii = '\r';

			if (packet.flags & (KBPACKET_FLAGS_LEFTCTRL | KBPACKET_FLAGS_RIGHTCTRL)) {
				if (!((packet.ascii > 'a' && packet.ascii < 'z') || (packet.ascii > 'A' && packet.ascii < 'Z')))
					continue;

				packet.ascii = packet.ascii > 'a' ? packet.ascii - 0x60 : packet.ascii - 0x40;
			}
		} else {
			switch (packet.keycode) {
				case KEYCODE_HOME:
					target = HOME_STR;
					break;
				case KEYCODE_INSERT:
					target = INSERT_STR;
					break;
				case KEYCODE_DELETE:
					target = DELETE_STR;
					break;
				case KEYCODE_END:
					target = END_STR;
					break;
				case KEYCODE_PAGEUP:
					target = PGUP_STR;
					break;
				case KEYCODE_PAGEDOWN:
					target = PGDN_STR;
					break;
				case KEYCODE_UP:
					target = UP_STR;
					break;
				case KEYCODE_DOWN:
					target = DOWN_STR;
					break;
				case KEYCODE_RIGHT:
					target = RIGHT_STR;
					break;
				case KEYCODE_LEFT:
					target = LEFT_STR;
					break;
				default:
					continue;
			}
			// TODO SWITCH OF SPECIAL KEYS
		}

		asciibuffer[0] = packet.ascii;
		size_t targetlen = strlen(target);
		if (targetlen == 0)
			continue;

		if (termios.c_lflag & ICANON) {
			bool flush = false;
			if (packet.ascii == '\b') {
				if (buffpos == 0)
					continue;
				--buffpos;
				buffer[buffpos] = '\0';
				if (termios.c_lflag & ECHO)
					console_write("\b \b", 3);
				continue;
			} else if (packet.ascii == '\n') {
				flush = true;
			}

			if (termios.c_lflag & ECHO)
				console_write(target, targetlen);

			for (size_t i = 0; i < targetlen; ++i) {
				buffer[buffpos++] = target[i];
				if (buffpos == THREAD_BUFFER_SIZE) {
					flush = true;
					break;
				}
			}

			if (flush) {
				MUTEX_ACQUIRE(&readmutex, false);
				ringbuffer_write(&readbuffer, buffer, buffpos);
				buffpos = 0;
				size_t datacount = RINGBUFFER_DATACOUNT(&readbuffer);
				if (datacount) {
					if (datacount >= termios.c_cc[VMIN])
						poll_event(&pollheader, POLLIN);

					semaphore_signal(&readsemaphore);
				}
				MUTEX_RELEASE(&readmutex);
			}
		} else {
			if (termios.c_lflag & ECHO)
				console_write(target, targetlen);

			MUTEX_ACQUIRE(&readmutex, false);
			ringbuffer_write(&readbuffer, target, targetlen);
			size_t datacount = RINGBUFFER_DATACOUNT(&readbuffer);
			if (datacount) {
				if (datacount >= termios.c_cc[VMIN])
					poll_event(&pollheader, POLLIN);

				semaphore_signal(&readsemaphore);
			}
			MUTEX_RELEASE(&readmutex);
		}
	}
}

static int read(int minor, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc) {
	size_t total = 0;
	for (;;) {
		if (semaphore_test(&readsemaphore) == false) {
			*readc = 0;
			if (termios.c_cc[VMIN] == 0)
				return 0;

			if (flags & V_FFLAGS_NONBLOCKING)
				return EAGAIN;

			int i = semaphore_wait(&readsemaphore, true);
			if (i)
				return i;
		}

		MUTEX_ACQUIRE(&readmutex, false);

		size_t read = ringbuffer_read(&readbuffer, buffer, size);
		size_t left = readbuffer.write - readbuffer.read;

		MUTEX_RELEASE(&readmutex);

		size -= read;
		buffer = (void *)((uintptr_t)buffer + read);
		total += read;

		if (size == 0 || total >= termios.c_cc[VMIN]) {
			if (left > 0)
				semaphore_signal(&readsemaphore);
			*readc = total;
			return 0;
		}
	}
}

static int write(int minor, void *buffer, size_t size, uintmax_t offset, int flags, size_t *writec) {
	*writec = size;
	console_write(buffer, size);
	return 0;
}

static int poll(int minor, polldata_t *data, int events) {
	int revents = 0;

	if (events & POLLOUT)
		events |= POLLOUT;

	MUTEX_ACQUIRE(&readmutex, false);

	size_t datacount = RINGBUFFER_DATACOUNT(&readbuffer);

	if ((events & POLLIN) && datacount >= termios.c_cc[VMIN])
		revents |= POLLIN;

	MUTEX_RELEASE(&readmutex);

	if (revents == 0 && data)
		poll_add(&pollheader, data, events);

	return revents;
}

static int ioctl(int minor, unsigned long req, void *arg, int *result) {
	switch (req) {
		case TIOCGWINSZ:
			size_t x, y, fbx, fby;
			term_getsize(&x, &y, &fbx, &fby);
			winsize_t *w = arg;
			w->ws_col = x;
			w->ws_row = y;
			w->ws_xpixel = fbx;
			w->ws_ypixel = fby;
			break;
		case TCGETS:
			*(termios_t *)arg = termios;
			break;
		case TCSETS:
			termios = *(termios_t *)arg;
			break;
		default:
			return ENOTTY;
	}

	return 0;
}

static int isatty(int minor) {
	return 0;
}

static devops_t devops = {
	.write = write,
	.read = read,
	.isatty = isatty,
	.poll = poll,
	.ioctl = ioctl
};

void console_init() {
	SEMAPHORE_INIT(&readsemaphore, 0);
	MUTEX_INIT(&writemutex);
	MUTEX_INIT(&readmutex);
	__assert(ringbuffer_init(&readbuffer, BUFFER_SIZE) == 0);
	__assert(devfs_register(&devops, "console", V_TYPE_CHDEV, DEV_MAJOR_CONSOLE, 0, 0644) == 0);
	thread = sched_newthread(consolethread, PAGE_SIZE * 16, 0, NULL, NULL);
	__assert(thread);
	sched_queue(thread);

	termios.c_iflag = ICRNL;
	termios.c_lflag = ECHO | ICANON | ISIG;
	termios.c_cc[VINTR] = 0x03;
    	termios.ibaud = 38400;
    	termios.obaud = 38400;
	termios.c_cc[VMIN] = 1;

	POLL_INITHEADER(&pollheader);
}
