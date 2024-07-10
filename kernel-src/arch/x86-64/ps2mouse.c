#include <arch/ps2.h>
#include <logging.h>
#include <kernel/mouse.h>

#define MOUSE_CMD_SAMPLERATE 0xF3
#define MOUSE_CMD_REPORTDATA 0xF4

#define MOUSE 0
#define MOUSE_Z 3
#define MOUSE_5B 4

static bool haswheel = false;
static bool fivebuttons = false;

static void mouse_setrate(int port, uint8_t rate) {
	device_write_response(port, MOUSE_CMD_SAMPLERATE);
	device_write_response(port, rate);
}

static uint8_t identify(int port) {
	__assert(device_write_response(port, DEVICE_CMD_IDENTIFY) == ACK);

	bool timeout;
	uint8_t b = read_data_timeout(5, &timeout);
	__assert(!timeout);

	return b;
}

static int datac; 

static uint8_t data[4];

static inline bool enoughdata() {
	if (!((haswheel && datac == 4) || (haswheel == false && datac == 3)))
		return false;
	else
		return true;
}

static mouse_t *mouse;

static void mouseisr() {
	data[datac] = inb(PS2_PORT_DATA);

	// check if the packet is bad
	if ((data[0] & 8) == 0) {
		datac = 0;
		return;
	}

	++datac;

	if (!enoughdata())
		return;

	mousepacket_t packet = {0};

	datac = 0;
	bool left = data[0] & 1;
	bool right = data[0] & 2;
	bool middle = data[0] & 4;
	bool b4 = data[3] & 0x10;
	bool b5 = data[3] & 0x20;

	packet.x = data[1] - (data[0] & 0x10 ? 0x100 : 0);
	packet.y = data[2] - (data[0] & 0x20 ? 0x100 : 0);
	packet.z = (data[3] & 0x7) * (data[3] & 0x8 ? -1 : 1);

	packet.flags |= left ? MOUSE_FLAG_LB : 0;
	packet.flags |= middle ? MOUSE_FLAG_MB : 0;
	packet.flags |= right ? MOUSE_FLAG_RB : 0;
	packet.flags |= b4 ? MOUSE_FLAG_B4 : 0;
	packet.flags |= b5 ? MOUSE_FLAG_B5 : 0;
	mouse_packet(mouse, &packet);
}


void ps2mouse_init() {
	if (identify(2) != MOUSE) {
		printf("Not a mouse!\n");
		return;
	}

	// check if mouse has scroll wheel
	mouse_setrate(2, 200);
	mouse_setrate(2, 100);
	mouse_setrate(2, 80);
	
	if (identify(2) == MOUSE_Z) {
		haswheel = true;

		// check if mouse has 5 buttons
		mouse_setrate(2, 200);
		mouse_setrate(2, 200);
		mouse_setrate(2, 80);

		if (identify(2) == MOUSE_5B)
			fivebuttons = true;
	}

	mouse_setrate(2, 60);
	int response = device_write_response(2, MOUSE_CMD_REPORTDATA);
	if (response != ACK) {
		printf("ps2mouse: expected ACK, got %x\n", response);
		return;
	}

	isr_t *isr = interrupt_allocate(mouseisr, arch_apic_eoi, IPL_MOUSE);
	__assert(isr);
	arch_ioapic_setirq(MOUSEIRQ, isr->id & 0xff, _cpu()->id, false);
	mouse = mouse_new();
	__assert(mouse);
	printf("ps2mouse: irq enabled with vector %u\n", isr->id & 0xff);
	printf("ps2mouse: wheel: %d five buttons: %d\n", haswheel, fivebuttons);
}
