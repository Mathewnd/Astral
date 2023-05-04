#include <kernel/keyboard.h>
#include <kernel/alloc.h>
#include <logging.h>
#include <errno.h>
#include <kernel/devfs.h>
#include <hashtable.h>

#define BUFFER_PACKET_CAPACITY 100
#define BUFFER_SIZE (BUFFER_PACKET_CAPACITY * sizeof(kbpacket_t))

static char asciitable[] = {
       0, '\033', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\n', 0, '/', 0, 0, '\n', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static char asciitableupper[] = {
       0, '\033', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\n', 0, '/', 0, 0, '\n', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static int currentkbnum;
keyboard_t *keyboard_console;

static keyboard_t *newkb() {
	keyboard_t *kb = alloc(sizeof(keyboard_t));
	if (kb == NULL)
		return NULL;

	if (ringbuffer_init(&kb->packetbuffer, BUFFER_SIZE)) {
		free(kb);
		return NULL;
	}

	SEMAPHORE_INIT(&kb->semaphore, 0);
	return kb;
}

static void destroykb(keyboard_t *kb) {
	ringbuffer_destroy(&kb->packetbuffer);
	free(kb);
}

void keyboard_sendpacket(keyboard_t *kb, kbpacket_t *packet) {
	interrupt_raiseipl(IPL_KEYBOARD);
	switch (packet->keycode) {
		case KEYCODE_LEFTCTRL:
			if (packet->flags & KBPACKET_FLAGS_RELEASED)
				kb->flags &= ~KBPACKET_FLAGS_LEFTCTRL;
			else
				kb->flags |= KBPACKET_FLAGS_LEFTCTRL;
			break;
		case KEYCODE_RIGHTCTRL:
			if (packet->flags & KBPACKET_FLAGS_RELEASED)
                                kb->flags &= ~KBPACKET_FLAGS_RIGHTCTRL;
                        else
				kb->flags |= KBPACKET_FLAGS_RIGHTCTRL;
			break;
		case KEYCODE_CAPSLOCK:
			if (packet->flags & KBPACKET_FLAGS_RELEASED)
                                kb->flags &= ~KBPACKET_FLAGS_CAPSLOCK;
                        else
				kb->flags |= KBPACKET_FLAGS_CAPSLOCK;
			break;
		case KEYCODE_LEFTALT:
			if (packet->flags & KBPACKET_FLAGS_RELEASED)
                                kb->flags &= ~KBPACKET_FLAGS_LEFTALT;
                        else
				kb->flags |= KBPACKET_FLAGS_LEFTALT;
			break;
		case KEYCODE_RIGHTALT:
			if (packet->flags & KBPACKET_FLAGS_RELEASED)
                                kb->flags &= ~KBPACKET_FLAGS_RIGHTALT;
                        else
				kb->flags |= KBPACKET_FLAGS_RIGHTALT;
			break;
		case KEYCODE_LEFTSHIFT:
			if (packet->flags & KBPACKET_FLAGS_RELEASED)
                                kb->flags &= ~KBPACKET_FLAGS_LEFTSHIFT;
                        else
				kb->flags |= KBPACKET_FLAGS_LEFTSHIFT;
			break;
		case KEYCODE_RIGHTSHIFT:
			if (packet->flags & KBPACKET_FLAGS_RELEASED)
                                kb->flags &= ~KBPACKET_FLAGS_RIGHTSHIFT;
                        else
				kb->flags |= KBPACKET_FLAGS_RIGHTSHIFT;
			break;
	}

	packet->flags |= kb->flags;
	char* table = asciitable;

	if ((packet->flags & KBPACKET_FLAGS_LEFTSHIFT) || (packet->flags & KBPACKET_FLAGS_RIGHTSHIFT))
		table = asciitableupper;

	packet->ascii = table[packet->keycode];

	if (ringbuffer_write(&kb->packetbuffer, packet, sizeof(kbpacket_t)) != 0)
		semaphore_signal(&kb->semaphore);

	if (ringbuffer_write(&keyboard_console->packetbuffer, packet, sizeof(kbpacket_t)) != 0)
		semaphore_signal(&keyboard_console->semaphore);

	interrupt_loweripl(IPL_KEYBOARD);
}

int keyboard_wait(keyboard_t *kb, kbpacket_t *packet) {
	int e = semaphore_wait(&kb->semaphore, true);
	if (e == EINTR)
		return e;
	interrupt_raiseipl(IPL_KEYBOARD);

	__assert(ringbuffer_read(&kb->packetbuffer, packet, sizeof(kbpacket_t)) == sizeof(kbpacket_t));

	interrupt_loweripl(IPL_KEYBOARD);
	return 0;
}

bool keyboard_get(keyboard_t *kb, kbpacket_t *packet) {
	interrupt_raiseipl(IPL_KEYBOARD);

	bool ok = ringbuffer_read(&kb->packetbuffer, packet, sizeof(kbpacket_t)) == sizeof(kbpacket_t);

	interrupt_loweripl(IPL_KEYBOARD);
	return ok;
}

static hashtable_t kbtable;

keyboard_t *keyboard_new() {
	keyboard_t *kb = newkb();
	if (kb == NULL)
		return NULL;

	if (hashtable_set(&kbtable, kb, &currentkbnum, sizeof(currentkbnum), true)) {
		destroykb(kb);
		return NULL;
	}

	// TODO keyboard device
	++currentkbnum;
	return kb;
}

void keyboard_init() {
	keyboard_console = newkb();
	__assert(keyboard_console);
	__assert(hashtable_init(&kbtable, 20) == 0);
}
