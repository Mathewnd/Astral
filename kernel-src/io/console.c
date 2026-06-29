#include <kernel/console.h>
#include <mutex.h>
#include <ringbuffer.h>
#include <logging.h>
#include <kernel/term.h>
#include <kernel/devfs.h>
#include <errno.h>
#include <termios.h>
#include <kernel/poll.h>
#include <kernel/tty.h>
#include <kernel/init.h>

static spinlock_t input_buffer_lock = SPINLOCK_INIT_VALUE;
static ringbuffer_t input_buffer;
static semaphore_t input_sem;

static mutex_t writemutex;
static thread_t *thread;
static tty_t *tty;
static bool locked;

static size_t console_ttywrite(void *internal, char *str, size_t count) {
	return console_write(str, count);
}

int console_set_lock(bool lock) {
	bool expected = !lock;
	if (!__atomic_compare_exchange_n(&locked, &expected, lock, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		return EBUSY;

	return 0;
}

bool console_is_tty(tty_t *p) {
	return p == tty;
}

void console_putc(char c) {
	if (c == '\n') {
		console_write("\r\n", 2);
	} else {
		console_write(&c, 1);
	}
}

size_t console_write(char *str, size_t count) {
	MUTEX_ACQUIRE(&writemutex);

	term_write(str, count);

	MUTEX_RELEASE(&writemutex);
	return count;
}

void console_process_events(input_event_t *events, int count) {
	long ipl = spinlock_acquire_raise_ipl(&input_buffer_lock, IPL_INPUT);
	for (int i = 0; i < count; ++i) {
		if (events[i].type != INPUT_EV_KEY || events[i].code >= INPUT_KEY_BTN_LEFT)
			continue;

		size_t written = ringbuffer_write(&input_buffer, &events[i], sizeof(input_event_t));
		if (written == sizeof(input_event_t))
			semaphore_signal(&input_sem);
	}
	spinlock_release_lower_ipl(&input_buffer_lock, ipl);
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

static const char key_to_ascii_normal[] = {
    [INPUT_KEY_ESC] = '\e',        [INPUT_KEY_1] = '1',
    [INPUT_KEY_2] = '2',           [INPUT_KEY_3] = '3',
    [INPUT_KEY_4] = '4',           [INPUT_KEY_5] = '5',
    [INPUT_KEY_6] = '6',           [INPUT_KEY_7] = '7',
    [INPUT_KEY_8] = '8',           [INPUT_KEY_9] = '9',
    [INPUT_KEY_0] = '0',           [INPUT_KEY_MINUS] = '-',
    [INPUT_KEY_EQUAL] = '=',       [INPUT_KEY_BACKSPACE] = '\b',
    [INPUT_KEY_TAB] = '\t',        [INPUT_KEY_Q] = 'q',
    [INPUT_KEY_W] = 'w',           [INPUT_KEY_E] = 'e',
    [INPUT_KEY_R] = 'r',           [INPUT_KEY_T] = 't',
    [INPUT_KEY_Y] = 'y',           [INPUT_KEY_U] = 'u',
    [INPUT_KEY_I] = 'i',           [INPUT_KEY_O] = 'o',
    [INPUT_KEY_P] = 'p',           [INPUT_KEY_LEFTBRACE] = '[',
    [INPUT_KEY_RIGHTBRACE] = ']',  [INPUT_KEY_ENTER] = '\r',
    [INPUT_KEY_A] = 'a',           [INPUT_KEY_S] = 's',
    [INPUT_KEY_D] = 'd',           [INPUT_KEY_F] = 'f',
    [INPUT_KEY_G] = 'g',           [INPUT_KEY_H] = 'h',
    [INPUT_KEY_J] = 'j',           [INPUT_KEY_K] = 'k',
    [INPUT_KEY_L] = 'l',           [INPUT_KEY_SEMICOLON] = ';',
    [INPUT_KEY_APOSTROPHE] = '\'', [INPUT_KEY_GRAVE] = '`',
    [INPUT_KEY_BACKSLASH] = '\\',  [INPUT_KEY_Z] = 'z',
    [INPUT_KEY_X] = 'x',           [INPUT_KEY_C] = 'c',
    [INPUT_KEY_V] = 'v',           [INPUT_KEY_B] = 'b',
    [INPUT_KEY_N] = 'n',           [INPUT_KEY_M] = 'm',
    [INPUT_KEY_COMMA] = ',',       [INPUT_KEY_DOT] = '.',
    [INPUT_KEY_SLASH] = '/',       [INPUT_KEY_KPASTERISK] = '*',
    [INPUT_KEY_SPACE] = ' ',       [INPUT_KEY_KP7] = '7',
    [INPUT_KEY_KP8] = '8',         [INPUT_KEY_KP9] = '9',
    [INPUT_KEY_KPMINUS] = '-',     [INPUT_KEY_KP4] = '4',
    [INPUT_KEY_KP5] = '5',         [INPUT_KEY_KP6] = '6',
    [INPUT_KEY_KPPLUS] = '+',      [INPUT_KEY_KP1] = '1',
    [INPUT_KEY_KP2] = '2',         [INPUT_KEY_KP3] = '3',
    [INPUT_KEY_KP0] = '0',         [INPUT_KEY_KPDOT] = '.',
    [INPUT_KEY_KPENTER] = '\r',    [INPUT_KEY_KPSLASH] = '/',
};

static const char key_to_ascii_shift[] = {
    [INPUT_KEY_ESC] = '\e',       [INPUT_KEY_1] = '!',
    [INPUT_KEY_2] = '@',          [INPUT_KEY_3] = '#',
    [INPUT_KEY_4] = '$',          [INPUT_KEY_5] = '%',
    [INPUT_KEY_6] = '^',          [INPUT_KEY_7] = '&',
    [INPUT_KEY_8] = '*',          [INPUT_KEY_9] = '(',
    [INPUT_KEY_0] = ')',          [INPUT_KEY_MINUS] = '_',
    [INPUT_KEY_EQUAL] = '+',      [INPUT_KEY_BACKSPACE] = '\b',
    [INPUT_KEY_TAB] = '\t',       [INPUT_KEY_Q] = 'Q',
    [INPUT_KEY_W] = 'W',          [INPUT_KEY_E] = 'E',
    [INPUT_KEY_R] = 'R',          [INPUT_KEY_T] = 'T',
    [INPUT_KEY_Y] = 'Y',          [INPUT_KEY_U] = 'U',
    [INPUT_KEY_I] = 'I',          [INPUT_KEY_O] = 'O',
    [INPUT_KEY_P] = 'P',          [INPUT_KEY_LEFTBRACE] = '{',
    [INPUT_KEY_RIGHTBRACE] = '}', [INPUT_KEY_ENTER] = '\r',
    [INPUT_KEY_A] = 'A',          [INPUT_KEY_S] = 'S',
    [INPUT_KEY_D] = 'D',          [INPUT_KEY_F] = 'F',
    [INPUT_KEY_G] = 'G',          [INPUT_KEY_H] = 'H',
    [INPUT_KEY_J] = 'J',          [INPUT_KEY_K] = 'K',
    [INPUT_KEY_L] = 'L',          [INPUT_KEY_SEMICOLON] = ':',
    [INPUT_KEY_APOSTROPHE] = '"', [INPUT_KEY_GRAVE] = '~',
    [INPUT_KEY_BACKSLASH] = '|',  [INPUT_KEY_Z] = 'Z',
    [INPUT_KEY_X] = 'X',          [INPUT_KEY_C] = 'C',
    [INPUT_KEY_V] = 'V',          [INPUT_KEY_B] = 'B',
    [INPUT_KEY_N] = 'N',          [INPUT_KEY_M] = 'M',
    [INPUT_KEY_COMMA] = '<',      [INPUT_KEY_DOT] = '>',
    [INPUT_KEY_SLASH] = '?',      [INPUT_KEY_KPASTERISK] = '*',
    [INPUT_KEY_SPACE] = ' ',      [INPUT_KEY_KP7] = '7',
    [INPUT_KEY_KP8] = '8',        [INPUT_KEY_KP9] = '9',
    [INPUT_KEY_KPMINUS] = '-',    [INPUT_KEY_KP4] = '4',
    [INPUT_KEY_KP5] = '5',        [INPUT_KEY_KP6] = '6',
    [INPUT_KEY_KPPLUS] = '+',     [INPUT_KEY_KP1] = '1',
    [INPUT_KEY_KP2] = '2',        [INPUT_KEY_KP3] = '3',
    [INPUT_KEY_KP0] = '0',        [INPUT_KEY_KPDOT] = '.',
    [INPUT_KEY_KPENTER] = '\r',   [INPUT_KEY_KPSLASH] = '/',
};

static void consolethread() {
	bool left_ctrl = false;
	bool right_ctrl = false;
	bool left_shift = false;
	bool right_shift = false;

	for (;;) {
		semaphore_wait(&input_sem, false);
		__assert(RINGBUFFER_DATACOUNT(&input_buffer) >= sizeof(input_event_t));

		input_event_t ev;
		__assert(ringbuffer_read(&input_buffer, &ev, sizeof(input_event_t)) == sizeof(input_event_t));

		if (ev.code == INPUT_KEY_LEFTCTRL) {
			left_ctrl = ev.value != 0;
			continue;
		} else if (ev.code == INPUT_KEY_RIGHTCTRL) {
			right_ctrl = ev.value != 0;
			continue;
		} else if (ev.code == INPUT_KEY_LEFTSHIFT) {
			left_shift = ev.value != 0;
			continue;
		} else if (ev.code == INPUT_KEY_RIGHTSHIFT) {
			right_shift = ev.value != 0;
			continue;
		}

		bool ctrl = left_ctrl || right_ctrl;
		bool shift = left_shift || right_shift;

		// we only care about key presses, not releases
		if (ev.value == 0)
			continue;

		char ascii = 0;
		if (shift) {
			if (ev.code < sizeof(key_to_ascii_shift))
				ascii = key_to_ascii_shift[ev.code];
		} else {
			if (ev.code < sizeof(key_to_ascii_normal))
				ascii = key_to_ascii_normal[ev.code];
		}

		if (ascii != 0) {
			if (ctrl) {
				// control characters
				if (!((ascii >= 'a' && ascii <= 'z') || (ascii >= 'A' && ascii <= '\\')))
					continue;

				ascii = ascii >= 'a' ? ascii - 0x60 : ascii - 0x40;
			}

			tty_process(tty, ascii);
		} else {
			// no ascii for key but we can send in a escape sequence for it
			char *target = NULL;
			switch (ev.code) {
				case INPUT_KEY_HOME:
					target = HOME_STR;
					break;
				case INPUT_KEY_INSERT:
					target = INSERT_STR;
					break;
				case INPUT_KEY_DELETE:
					target = DELETE_STR;
					break;
				case INPUT_KEY_END:
					target = END_STR;
					break;
				case INPUT_KEY_PAGEUP:
					target = PGUP_STR;
					break;
				case INPUT_KEY_PAGEDOWN:
					target = PGDN_STR;
					break;
				case INPUT_KEY_UP:
					target = UP_STR;
					break;
				case INPUT_KEY_DOWN:
					target = DOWN_STR;
					break;
				case INPUT_KEY_RIGHT:
					target = RIGHT_STR;
					break;
				case INPUT_KEY_LEFT:
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
	SEMAPHORE_INIT(&input_sem, 0);
	ringbuffer_init(&input_buffer, 128 * sizeof(input_event_t));

	MUTEX_INIT(&writemutex);

	thread = sched_newthread(consolethread, PAGE_SIZE * 16, 0, NULL, NULL);
	__assert(thread);
	sched_queue(thread);

	tty = tty_create("console", console_ttywrite, NULL, NULL, NULL, NULL);
	__assert(tty);

	size_t x, y, fbx, fby;
	term_getsize(&x, &y, &fbx, &fby);
	tty->winsize.ws_col = x;
	tty->winsize.ws_row = y;
	tty->winsize.ws_xpixel = fbx;
	tty->winsize.ws_ypixel = fby;

	logging_sethook(console_putc);
}

INIT_ROUTINE_DEFINE(console, INIT_ROUTINE_FLAGS_NONE, console_init, scheduler);
