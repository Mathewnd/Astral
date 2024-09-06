#include <arch/ps2.h>
#include <logging.h>
#include <kernel/mouse.h>

#define PS2_MOUSE_CMD_SAMPLERATE 0xF3

#define PS2_MOUSE 0
#define PS2_MOUSE_Z 3
#define PS2_MOUSE_5B 4

static bool haswheel = false;
static bool fivebuttons = false;

static void ps2_mouse_setrate(int port, uint8_t rate) {
	if (ps2_device_write_ok(port, PS2_MOUSE_CMD_SAMPLERATE) == false) {
		printf("ps2mouse: setting rate at port %d failed\n", port);
	}

	if (ps2_device_write_ok(port, rate) == false) {
		printf("ps2mouse: setting rate at port %d failed\n", port);
	}
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

#define DO_IDENTIFY_CHECK(step) \
	if (ps2_identify(2, identity) == false) { \
		printf("ps2: ps2mouse_init: failed to identify mouse at port %d (step \"%s\")\n", port, step); \
		return; \
	}

void ps2mouse_init() {
	int port = 2;
	uint8_t identity[2];

	DO_IDENTIFY_CHECK("first");

	if (identity[0] != PS2_MOUSE) {
		printf("Not a mouse!\n");
		return;
	}

	// check if mouse has scroll wheel
	ps2_mouse_setrate(2, 200);
	ps2_mouse_setrate(2, 100);
	ps2_mouse_setrate(2, 80);

	DO_IDENTIFY_CHECK("has scroll");
	
	if (identity[0] == PS2_MOUSE_Z) {
		haswheel = true;

		// check if mouse has 5 buttons
		ps2_mouse_setrate(2, 200);
		ps2_mouse_setrate(2, 200);
		ps2_mouse_setrate(2, 80);

		DO_IDENTIFY_CHECK("5 buttons");

		if (identity[0] == PS2_MOUSE_5B)
			fivebuttons = true;
	}

	ps2_mouse_setrate(2, 60);

	isr_t *isr = interrupt_allocate(mouseisr, arch_apic_eoi, IPL_MOUSE);
	__assert(isr);
	arch_ioapic_setirq(PS2_MOUSEIRQ, isr->id & 0xff, _cpu()->id, false);
	mouse = mouse_new();
	__assert(mouse);
	printf("ps2mouse: irq enabled with vector %u\n", isr->id & 0xff);
	printf("ps2mouse: wheel: %d five buttons: %d\n", haswheel, fivebuttons);
}
