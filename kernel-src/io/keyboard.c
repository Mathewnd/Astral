#include <kernel/keyboard.h>
#include <kernel/alloc.h>
#include <logging.h>
#include <errno.h>
#include <kernel/devfs.h>
#include <hashtable.h>

#define BUFFER_PACKET_CAPACITY 100
#define BUFFER_SIZE (BUFFER_PACKET_CAPACITY * sizeof(kbpacket_t))

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
