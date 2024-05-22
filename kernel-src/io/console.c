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
#include <kernel/tty.h>

static mutex_t writemutex;
static thread_t *thread;
static tty_t *tty;

size_t console_write(char *str, size_t count) {
	MUTEX_ACQUIRE(&writemutex, false);

	term_write(str, count);

	MUTEX_RELEASE(&writemutex);
	return count;
}

size_t console_ttywrite(void *internal, char *str, size_t count) {
	return console_write(str, count);
}

void console_putc(char c) {
	console_write(&c, 1);
}

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
	for (;;) {
		kbpacket_t packet;
		keyboard_wait(keyboard_console, &packet);
		if (packet.flags & KBPACKET_FLAGS_RELEASED)
			continue;

		if (packet.ascii) {
			// control characters
			if (packet.flags & (KBPACKET_FLAGS_LEFTCTRL | KBPACKET_FLAGS_RIGHTCTRL)) {
				if (!((packet.ascii >= 'a' && packet.ascii <= 'z') || (packet.ascii >= 'A' && packet.ascii <= '\\')))
					continue;

				packet.ascii = packet.ascii >= 'a' ? packet.ascii - 0x60 : packet.ascii - 0x40;
			}

			tty_process(tty, packet.ascii);
		} else {
			// no ascii for key but we can send in a escape sequence for it
			char *target = NULL;
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

			size_t len = strlen(target);
			for (int i = 0; i < len; ++i)
				tty_process(tty, target[i]);
		}
	}
}

void console_init() {
	MUTEX_INIT(&writemutex);

	thread = sched_newthread(consolethread, PAGE_SIZE * 16, 0, NULL, NULL);
	__assert(thread);
	sched_queue(thread);

	tty = tty_create("console", console_ttywrite, NULL, NULL);
	__assert(tty);

	size_t x, y, fbx, fby;
	term_getsize(&x, &y, &fbx, &fby);
	tty->winsize.ws_col = x;
	tty->winsize.ws_row = y;
	tty->winsize.ws_xpixel = fbx;
	tty->winsize.ws_ypixel = fby;
}
