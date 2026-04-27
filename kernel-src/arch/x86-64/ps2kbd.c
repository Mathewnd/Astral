#include <arch/apic.h>
#include <arch/cpu.h>
#include <arch/io.h>
#include <arch/ps2.h>
#include <arch/ps2kbd.h>
#include <kernel/input.h>
#include <kernel/interrupt.h>
#include <logging.h>
#include <stdbool.h>

static uint8_t codes[128] = {
 	INPUT_KEY_RESERVED,
 	INPUT_KEY_ESC,
 	INPUT_KEY_1,
 	INPUT_KEY_2,
 	INPUT_KEY_3,
 	INPUT_KEY_4,
 	INPUT_KEY_5,
 	INPUT_KEY_6,
 	INPUT_KEY_7,
 	INPUT_KEY_8,
 	INPUT_KEY_9,
 	INPUT_KEY_0,
 	INPUT_KEY_MINUS,
 	INPUT_KEY_EQUAL,
 	INPUT_KEY_BACKSPACE,
 	INPUT_KEY_TAB,
 	INPUT_KEY_Q,
 	INPUT_KEY_W,
 	INPUT_KEY_E,
 	INPUT_KEY_R,
 	INPUT_KEY_T,
 	INPUT_KEY_Y,
 	INPUT_KEY_U,
 	INPUT_KEY_I,
 	INPUT_KEY_O,
 	INPUT_KEY_P,
 	INPUT_KEY_LEFTBRACE,
 	INPUT_KEY_RIGHTBRACE,
 	INPUT_KEY_ENTER,
 	INPUT_KEY_LEFTCTRL,
 	INPUT_KEY_A,
 	INPUT_KEY_S,
 	INPUT_KEY_D,
 	INPUT_KEY_F,
 	INPUT_KEY_G,
 	INPUT_KEY_H,
 	INPUT_KEY_J,
 	INPUT_KEY_K,
 	INPUT_KEY_L,
 	INPUT_KEY_SEMICOLON,
 	INPUT_KEY_APOSTROPHE,
 	INPUT_KEY_GRAVE,
 	INPUT_KEY_LEFTSHIFT,
 	INPUT_KEY_BACKSLASH,
 	INPUT_KEY_Z,
 	INPUT_KEY_X,
 	INPUT_KEY_C,
 	INPUT_KEY_V,
 	INPUT_KEY_B,
 	INPUT_KEY_N,
 	INPUT_KEY_M,
 	INPUT_KEY_COMMA,
 	INPUT_KEY_DOT,
 	INPUT_KEY_SLASH,
 	INPUT_KEY_RIGHTSHIFT,
 	INPUT_KEY_KPASTERISK,
 	INPUT_KEY_LEFTALT,
 	INPUT_KEY_SPACE,
 	INPUT_KEY_CAPSLOCK,
 	INPUT_KEY_F1,
 	INPUT_KEY_F2,
 	INPUT_KEY_F3,
 	INPUT_KEY_F4,
 	INPUT_KEY_F5,
 	INPUT_KEY_F6,
 	INPUT_KEY_F7,
 	INPUT_KEY_F8,
 	INPUT_KEY_F9,
 	INPUT_KEY_F10,
 	INPUT_KEY_NUMLOCK,
 	INPUT_KEY_SCROLLLOCK,
 	INPUT_KEY_KP7,
 	INPUT_KEY_KP8,
 	INPUT_KEY_KP9,
 	INPUT_KEY_KPMINUS,
 	INPUT_KEY_KP4,
 	INPUT_KEY_KP5,
 	INPUT_KEY_KP6,
 	INPUT_KEY_KPPLUS,
 	INPUT_KEY_KP1,
 	INPUT_KEY_KP2,
	INPUT_KEY_KP3,
	INPUT_KEY_KP0,
	INPUT_KEY_KPDOT,
	0, 0, 0,
	INPUT_KEY_F11,
	INPUT_KEY_F12
};

static uint8_t extendedcodes[128] = {
	[0x1C] = INPUT_KEY_KPENTER,
	[0x1D] = INPUT_KEY_RIGHTCTRL,
	[0x35] = INPUT_KEY_KPSLASH, // k/
	[0x38] = INPUT_KEY_RIGHTALT, // altgr
	[0x47] = INPUT_KEY_HOME, // home
	[0x48] = INPUT_KEY_UP, // up
	[0x49] = INPUT_KEY_PAGEUP, // page up
	[0x4B] = INPUT_KEY_LEFT, // left
	[0x4D] = INPUT_KEY_RIGHT, // right
	[0x4F] = INPUT_KEY_END, // end
	[0x50] = INPUT_KEY_DOWN, // down
	[0x51] = INPUT_KEY_PAGEDOWN, // page down
	[0x52] = INPUT_KEY_INSERT, // insert
	[0x53] = INPUT_KEY_DELETE // delete
};

#define KEYBOARDIRQ 1

static input_device_t *input_dev;

static bool extended = false;

static void kbdisr(isr_t *isr, context_t *ctx) {
	uint8_t scancode = inb(PS2_PORT_DATA);
	if (scancode == 0xE0) {
		extended = true;
		return;
	}

	uint8_t *table = codes;
	if (extended) {
		table = extendedcodes;
		extended = false;
	}

	bool released = (scancode & 0x80) != 0;
	scancode &= 0x7f;

	input_event_t event;
	event.type = INPUT_EV_KEY;
	event.code = table[scancode];
	if (event.code == INPUT_KEY_RESERVED)
		return;

	event.value = released ? 0 : 1;
	input_queue_packet(input_dev, &event, 1);
}

void ps2kbd_init() {
	isr_t *isr = interrupt_allocate(kbdisr, arch_apic_eoi, IPL_INPUT);
	__assert(isr);
	arch_ioapic_setirq(KEYBOARDIRQ, isr->id & 0xff, current_cpu_id(), false);

	input_dev = input_new();
	__assert(input_dev);
	input_dev->id_bus = INPUT_DEVICE_BUS_I8042;
	snprintf(input_dev->name, sizeof(input_dev->name), "PS/2 Keyboard");
	input_dev->id_version = 1;
	input_dev->ver_major = 1;
	input_dev->ver_minor = 0;
	input_dev->ver_patch = 0;

	bitmap_set(&input_dev->ev_bits, INPUT_EV_KEY, 1);
	for (int i = 0; i < 128; ++i) {
		if (codes[i] != INPUT_KEY_RESERVED)
			bitmap_set(&input_dev->key_bits, codes[i], 1);
		if (extendedcodes[i] != INPUT_KEY_RESERVED)
			bitmap_set(&input_dev->key_bits, extendedcodes[i], 1);
	}

	printf("ps2kbd: irq enabled with vector %u\n", isr->id & 0xff);
}
