#include <arch/ps2.h>
#include <logging.h>
#include <kernel/input.h>

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

static input_device_t *input_dev;

static bool last_left = false;
static bool last_right = false;
static bool last_middle = false;
static bool last_side = false;
static bool last_extra = false;

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

	datac = 0;

	int x = data[1] - (data[0] & 0x10 ? 0x100 : 0);
	int y = data[2] - (data[0] & 0x20 ? 0x100 : 0);
	int z = 0;

	if (haswheel) {
		if (fivebuttons)
			z = (data[3] & 0x7) * (data[3] & 0x8 ? -1 : 1);
		else
			z = (int8_t)data[3];
	}

	bool left = (data[0] & 1) != 0;
	bool right = (data[0] & 2) != 0;
	bool middle = (data[0] & 4) != 0;
	bool side = false, extra = false;

	if (fivebuttons) {
		side = (data[3] & 16) != 0;
		extra = (data[3] & 32) != 0;
	}

	input_event_t events[8];
	int eventcount = 0;

#define EMIT_EVENT(TYPE, CODE, VALUE) \
	do { \
		events[eventcount].type = (TYPE); \
		events[eventcount].code = (CODE); \
		events[eventcount].value = (VALUE); \
		eventcount++; \
	} while (0)

	if (x != 0)
		EMIT_EVENT(INPUT_EV_REL, INPUT_REL_X, x);
	if (y != 0)
		EMIT_EVENT(INPUT_EV_REL, INPUT_REL_Y, -y);
	if (z != 0)
		EMIT_EVENT(INPUT_EV_REL, INPUT_REL_WHEEL, z);

	if (left != last_left)
		EMIT_EVENT(INPUT_EV_KEY, INPUT_KEY_BTN_LEFT, left ? 1 : 0);
	if (right != last_right)
		EMIT_EVENT(INPUT_EV_KEY, INPUT_KEY_BTN_RIGHT, right ? 1 : 0);
	if (middle != last_middle)
		EMIT_EVENT(INPUT_EV_KEY, INPUT_KEY_BTN_MIDDLE, middle ? 1 : 0);
	if (side != last_side)
		EMIT_EVENT(INPUT_EV_KEY, INPUT_KEY_BTN_SIDE, side ? 1 : 0);
	if (extra != last_extra)
		EMIT_EVENT(INPUT_EV_KEY, INPUT_KEY_BTN_EXTRA, extra ? 1 : 0);

	last_left = left;
	last_right = right;
	last_middle = middle;
	last_side = side;
	last_extra = extra;

#undef EMIT_EVENT

	input_queue_packet(input_dev, events, eventcount);
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
		printf("ps2: ps2mouse_init: not a mouse!\n");
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

	isr_t *isr = interrupt_allocate(mouseisr, arch_apic_eoi, IPL_INPUT);
	__assert(isr);
	arch_ioapic_setirq(PS2_MOUSEIRQ, isr->id & 0xff, current_cpu_id(), false);

	input_dev = input_new();
	__assert(input_dev);
	input_dev->id_bus = INPUT_DEVICE_BUS_I8042;
	snprintf(input_dev->name, sizeof(input_dev->name), "PS/2 Mouse");
	input_dev->id_version = 1;
	input_dev->ver_major = 1;
	input_dev->ver_minor = 0;
	input_dev->ver_patch = 0;

	bitmap_set(&input_dev->ev_bits, INPUT_EV_KEY, 1);
	bitmap_set(&input_dev->key_bits, INPUT_KEY_BTN_LEFT, 1);
	bitmap_set(&input_dev->key_bits, INPUT_KEY_BTN_RIGHT, 1);
	bitmap_set(&input_dev->key_bits, INPUT_KEY_BTN_MIDDLE, 1);

	if (fivebuttons) {
		bitmap_set(&input_dev->key_bits, INPUT_KEY_BTN_SIDE, 1);
		bitmap_set(&input_dev->key_bits, INPUT_KEY_BTN_EXTRA, 1);
	}

	bitmap_set(&input_dev->ev_bits, INPUT_EV_REL, 1);
	bitmap_set(&input_dev->rel_bits, INPUT_REL_X, 1);
	bitmap_set(&input_dev->rel_bits, INPUT_REL_Y, 1);

	if (haswheel)
		bitmap_set(&input_dev->rel_bits, INPUT_REL_WHEEL, 1);

	printf("ps2mouse: irq enabled with vector %u\n", isr->id & 0xff);
	printf("ps2mouse: wheel: %d five buttons: %d\n", haswheel, fivebuttons);
}
