#include <kernel/mouse.h>
#include <kernel/alloc.h>
#include <logging.h>
#include <errno.h>
#include <kernel/devfs.h>
#include <hashtable.h>
#include <kernel/poll.h>

#define BUFFER_PACKET_CAPACITY 100
#define BUFFER_SIZE (BUFFER_PACKET_CAPACITY * sizeof(mousepacket_t))

static int currentnum;

static mouse_t *newmouse() {
	mouse_t *mouse = alloc(sizeof(mouse_t));
	if (mouse == NULL)
		return NULL;

	if (ringbuffer_init(&mouse->packetbuffer, BUFFER_SIZE)) {
		free(mouse);
		return NULL;
	}

	POLL_INITHEADER(&mouse->pollheader);
	SPINLOCK_INIT(mouse->lock);
	MUTEX_INIT(&mouse->readmutex);

	return mouse;
}

static void destroymouse(mouse_t *mouse) {
	ringbuffer_destroy(&mouse->packetbuffer);
	free(mouse);
}

void mouse_packet(mouse_t *mouse, mousepacket_t *packet) {
	int oldipl = interrupt_raiseipl(IPL_MOUSE);
	spinlock_acquire(&mouse->lock);

	ringbuffer_write(&mouse->packetbuffer, packet, sizeof(mousepacket_t));
	poll_event(&mouse->pollheader, POLLIN);

	spinlock_release(&mouse->lock);
	interrupt_loweripl(oldipl);
}

static hashtable_t mousetable;
static mutex_t tablelock;

static mouse_t* getmouse(int mouse) {
	void *mousep = NULL;
	MUTEX_ACQUIRE(&tablelock, false);
	hashtable_get(&mousetable, &mousep, &mouse, sizeof(mouse));
	MUTEX_RELEASE(&tablelock);
	return mousep;
}

static int internalpoll(mouse_t *mouse, polldata_t *data, int events) {
	int revents = 0;

	if ((events & POLLIN) && RINGBUFFER_DATACOUNT(&mouse->packetbuffer))
		revents |= POLLIN;

	if (revents == 0 && data)
		poll_add(&mouse->pollheader, data, events);

	return revents;
}

static int read(int minor, void *buffer, size_t size, uintmax_t offset, int flags, size_t *readc) {
	mouse_t *mouse = getmouse(minor);
	if (mouse == NULL)
		return ENODEV;

	int error = 0;
	*readc = 0;
	size_t packetcount = size / sizeof(mousepacket_t);
	if (packetcount == 0)
		return error;

	MUTEX_ACQUIRE(&mouse->readmutex, false);
	// wait until there is data to read or return if nonblock
	for (;;) {
		polldesc_t desc = {0};
		error = poll_initdesc(&desc, 1);
		if (error)
			goto leave;

		// disable interrupts here as to not lose any interrupts
		// and because we cannot sleep with a raised ipl
		bool oldintstatus = interrupt_set(false);
		spinlock_acquire(&mouse->lock);

		int revents = internalpoll(mouse, &desc.data[0], POLLIN);

		if (revents) {
			spinlock_release(&mouse->lock);
			interrupt_set(oldintstatus);
			poll_leave(&desc);
			poll_destroydesc(&desc);
			break;
		}

		if (revents == 0 && flags & V_FFLAGS_NONBLOCKING) {
			spinlock_release(&mouse->lock);
			interrupt_set(oldintstatus);
			poll_leave(&desc);
			poll_destroydesc(&desc);
			error = EAGAIN;
			goto leave;
		}

		spinlock_release(&mouse->lock);
		error = poll_dowait(&desc, 0);
		interrupt_set(oldintstatus);

		poll_leave(&desc);
		poll_destroydesc(&desc);

		if (error)
			goto leave;
	}

	*readc = ringbuffer_read(&mouse->packetbuffer, buffer, sizeof(mousepacket_t) * packetcount);
	__assert(*readc);

	leave:
	MUTEX_RELEASE(&mouse->readmutex);

	return error;
}

static int poll(int minor, polldata_t *data, int events) {
	mouse_t *mouse = getmouse(minor);
	if (mouse == NULL)
		return POLLERR;

	MUTEX_ACQUIRE(&mouse->readmutex, false);

	long ipl = interrupt_raiseipl(IPL_MOUSE);
	spinlock_acquire(&mouse->lock);
	int revents = internalpoll(mouse, data, events);
	spinlock_release(&mouse->lock);
	interrupt_loweripl(ipl);

	MUTEX_RELEASE(&mouse->readmutex);

	return revents;
}

static devops_t devops = {
	.read = read,
	.poll = poll
};

mouse_t *mouse_new() {
	mouse_t *mouse = newmouse();
	if (mouse == NULL)
		return NULL;

	MUTEX_ACQUIRE(&tablelock, false);

	if (hashtable_set(&mousetable, mouse, &currentnum, sizeof(currentnum), true)) {
		MUTEX_RELEASE(&tablelock);
		destroymouse(mouse);
		return NULL;
	}

	MUTEX_RELEASE(&tablelock);

	char name[12];

	snprintf(name, 12, "mouse%d", currentnum);
	__assert(devfs_register(&devops, name, V_TYPE_CHDEV, DEV_MAJOR_MOUSE, currentnum, 0600, NULL) == 0);

	++currentnum;
	return mouse;
}

void mouse_init() {
	MUTEX_INIT(&tablelock);
	__assert(hashtable_init(&mousetable, 20) == 0);
}

