#include <kernel/keyboard.h>
#include <kernel/alloc.h>
#include <logging.h>
#include <errno.h>
#include <kernel/devfs.h>
#include <hashtable.h>
#include <kernel/poll.h>
#include <kernel/usercopy.h>

#define BUFFER_PACKET_CAPACITY 100
#define BUFFER_SIZE (BUFFER_PACKET_CAPACITY * sizeof(kbpacket_t))

static char asciitable[] = {
       0, '\033', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\r', 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\r', 0, '/', 0, 0, '\r', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static char asciitableupper[] = {
       0, '\033', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\r', 0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '\r', 0, '/', 0, 0, '\r', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
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
	long ipl = interrupt_raiseipl(IPL_KEYBOARD);
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

	spinlock_acquire(&kb->lock);

	if (ringbuffer_write(&kb->packetbuffer, packet, sizeof(kbpacket_t)) != 0) {
		semaphore_signal(&kb->semaphore);
		poll_event(&kb->pollheader, POLLIN);
	}
	spinlock_release(&kb->lock);

	spinlock_acquire(&keyboard_console->lock);

	if (ringbuffer_write(&keyboard_console->packetbuffer, packet, sizeof(kbpacket_t)) != 0) {
		semaphore_signal(&keyboard_console->semaphore);
		poll_event(&keyboard_console->pollheader, POLLIN);
	}

	spinlock_release(&keyboard_console->lock);

	interrupt_loweripl(ipl);
}

int keyboard_wait(keyboard_t *kb, kbpacket_t *packet) {
	int e = semaphore_wait(&kb->semaphore, true);
	if (e == SCHED_WAKEUP_REASON_INTERRUPTED)
		return e;
	long ipl = interrupt_raiseipl(IPL_KEYBOARD);

	spinlock_acquire(&kb->lock);
	__assert(ringbuffer_read(&kb->packetbuffer, packet, sizeof(kbpacket_t)) == sizeof(kbpacket_t));
	spinlock_release(&kb->lock);

	interrupt_loweripl(ipl);
	return 0;
}

bool keyboard_get(keyboard_t *kb, kbpacket_t *packet) {
	if (semaphore_test(&kb->semaphore) == false)
		return false;

	long ipl = interrupt_raiseipl(IPL_KEYBOARD);

	spinlock_acquire(&kb->lock);
	bool ok = ringbuffer_read(&kb->packetbuffer, packet, sizeof(kbpacket_t)) == sizeof(kbpacket_t);
	spinlock_release(&kb->lock);

	interrupt_loweripl(ipl);
	return ok;
}

static hashtable_t kbtable;
static mutex_t tablelock;

static keyboard_t* getkb(int kb) {
	void *kbp = NULL;
	MUTEX_ACQUIRE(&tablelock, false);
	hashtable_get(&kbtable, &kbp, &kb, sizeof(kb));
	MUTEX_RELEASE(&tablelock);
	return kbp;
}

static int read(int minor, iovec_iterator_t *iovec_iterator, size_t size, uintmax_t offset, int flags, size_t *readc) {
	keyboard_t *kb = getkb(minor);
	if (kb == NULL)
		return ENODEV;

	*readc = 0;
	size_t count = size / sizeof(kbpacket_t);
	if (count == 0)
		return 0;

	for (int i = 0; i < count; ++i) {
		kbpacket_t packet;

		bool ok = keyboard_get(kb, &packet);
		if (ok == false) {
			if (i == 0 && (flags & V_FFLAGS_NONBLOCKING) == 0) {
				int error = keyboard_wait(kb, &packet);
				if (error)
					return error;

				error = iovec_iterator_copy_from_buffer(iovec_iterator, &packet, sizeof(kbpacket_t));

				*readc += error ? 0 : sizeof(kbpacket_t);
				return error;
			}

			break;
		}

		int error = iovec_iterator_copy_from_buffer(iovec_iterator, &packet, sizeof(kbpacket_t));
		if (error)
			return error;

		*readc += sizeof(kbpacket_t);
	}

	return 0;
}

static int poll(int minor, polldata_t *data, int events) {
	int revents = 0;
	keyboard_t *kb = getkb(minor);
	if (kb == NULL)
		return POLLERR;

	long ipl = interrupt_raiseipl(IPL_KEYBOARD);
	spinlock_acquire(&kb->lock);
	if (events & POLLIN) {
		size_t dataleft = RINGBUFFER_DATACOUNT(&kb->packetbuffer);
		if (dataleft)
			revents |= POLLIN;
	}

	if (revents == 0 && data)
		poll_add(&kb->pollheader, data, events);

	spinlock_release(&kb->lock);
	interrupt_loweripl(ipl);

	return revents;
}

static devops_t devops = {
	.read = read,
	.poll = poll
};

keyboard_t *keyboard_new() {
	keyboard_t *kb = newkb();
	if (kb == NULL)
		return NULL;

	MUTEX_ACQUIRE(&tablelock, false);

	if (hashtable_set(&kbtable, kb, &currentkbnum, sizeof(currentkbnum), true)) {
		MUTEX_RELEASE(&tablelock);
		destroykb(kb);
		return NULL;
	}

	MUTEX_RELEASE(&tablelock);

	char name[12];
	snprintf(name, 12, "keyboard%d", currentkbnum);

	__assert(devfs_register(&devops, name, V_TYPE_CHDEV, DEV_MAJOR_KEYBOARD, currentkbnum, 0666, NULL) == 0);

	POLL_INITHEADER(&kb->pollheader);
	++currentkbnum;
	return kb;
}

void keyboard_init() {
	MUTEX_INIT(&tablelock);
	keyboard_console = newkb();
	__assert(keyboard_console);
	__assert(hashtable_init(&kbtable, 20) == 0);
}
