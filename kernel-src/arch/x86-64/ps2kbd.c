#include <arch/ps2kbd.h>
#include <stdbool.h>
#include <arch/io.h>
#include <arch/cpu.h>
#include <kernel/interrupt.h>
#include <arch/apic.h>
#include <kernel/keyboard.h>
#include <arch/ps2.h>
#include <logging.h>

static char codes[128] = {
	KEYCODE_RESERVED,
	KEYCODE_ESCAPE,
	KEYCODE_1,
	KEYCODE_2,
	KEYCODE_3,
	KEYCODE_4,
	KEYCODE_5,
	KEYCODE_6,
	KEYCODE_7,
	KEYCODE_8,
	KEYCODE_9,
	KEYCODE_0,
	KEYCODE_MINUS,
	KEYCODE_EQUAL,
	KEYCODE_BACKSPACE,
	KEYCODE_TAB,
	KEYCODE_Q,
	KEYCODE_W,
	KEYCODE_E,
	KEYCODE_R,
	KEYCODE_T,
	KEYCODE_Y,
	KEYCODE_U,
	KEYCODE_I,
	KEYCODE_O,
	KEYCODE_P,
	KEYCODE_LEFTBRACE,
	KEYCODE_RIGHTBRACE,
	KEYCODE_ENTER,
	KEYCODE_LEFTCTRL,
	KEYCODE_A,
	KEYCODE_S,
	KEYCODE_D,
	KEYCODE_F,
	KEYCODE_G,
	KEYCODE_H,
	KEYCODE_J,
	KEYCODE_K,
	KEYCODE_L,
	KEYCODE_SEMICOLON,
	KEYCODE_APOSTROPHE,
	KEYCODE_GRAVE,
	KEYCODE_LEFTSHIFT,
	KEYCODE_BACKSLASH,
	KEYCODE_Z,
	KEYCODE_X,
	KEYCODE_C,
	KEYCODE_V,
	KEYCODE_B,
	KEYCODE_N,
	KEYCODE_M,
	KEYCODE_COMMA,
	KEYCODE_DOT,
	KEYCODE_SLASH,
	KEYCODE_RIGHTSHIFT,
	KEYCODE_KEYPADASTERISK,
	KEYCODE_LEFTALT,
	KEYCODE_SPACE,
	KEYCODE_CAPSLOCK,
	KEYCODE_F1,
	KEYCODE_F2,
	KEYCODE_F3,
	KEYCODE_F4,
	KEYCODE_F5,
	KEYCODE_F6,
	KEYCODE_F7,
	KEYCODE_F8,
	KEYCODE_F9,
	KEYCODE_F10,
	KEYCODE_NUMLOCK,
	KEYCODE_SCROLLLOCK,
	KEYCODE_KEYPAD7,
	KEYCODE_KEYPAD8,
	KEYCODE_KEYPAD9,
	KEYCODE_KEYPADMINUS,
	KEYCODE_KEYPAD4,
	KEYCODE_KEYPAD5,
	KEYCODE_KEYPAD6,
	KEYCODE_KEYPADPLUS,
	KEYCODE_KEYPAD1,
	KEYCODE_KEYPAD2,
	KEYCODE_KEYPAD3,
	KEYCODE_KEYPAD0,
	KEYCODE_KEYPADDOT,
	0, 0, 0,
	KEYCODE_F11,
	KEYCODE_F12
};

static char extendedcodes[128] = {
	[0x1C] = KEYCODE_KEYPADENTER,
	[0x1D] = KEYCODE_RIGHTCTRL, 
	[0x35] = KEYCODE_KEYPADSLASH, // k/
	[0x38] = KEYCODE_RIGHTALT, // altgr
	[0x47] = KEYCODE_HOME, // home
	[0x48] = KEYCODE_UP, // up
	[0x49] = KEYCODE_PAGEUP, // page up
	[0x4B] = KEYCODE_LEFT, // left
	[0x4D] = KEYCODE_RIGHT, // right
	[0x4F] = KEYCODE_END, // end
	[0x50] = KEYCODE_DOWN, // down
	[0x51] = KEYCODE_PAGEDOWN, // page down
	[0x52] = KEYCODE_INSERT, // insert
	[0x53] = KEYCODE_DELETE // delete
};

#define KEYBOARDIRQ 1

keyboard_t *kb;
static bool extended = false;

static void kbdisr(isr_t *isr, context_t *ctx) {
	uint8_t scancode = inb(PS2_PORT_DATA);
	if (scancode == 0xE0) {
		extended = true;
		return;
	}

	kbpacket_t packet;

	packet.flags = 0;

	if (scancode & 0x80) {
		packet.flags = KBPACKET_FLAGS_RELEASED;
		scancode &= 0x7F;
	}

	char *tab = codes;

	if (extended) {
		tab = extendedcodes;
		extended = false;
	}

	packet.keycode = tab[scancode];

	if (!packet.keycode)
		return;

	keyboard_sendpacket(kb, &packet);
}

void ps2kbd_init() {
	isr_t *isr = interrupt_allocate(kbdisr, arch_apic_eoi, IPL_KEYBOARD);
	__assert(isr);
	arch_ioapic_setirq(KEYBOARDIRQ, isr->id & 0xff, current_cpu_id(), false);
	kb = keyboard_new();
	__assert(kb);
	printf("ps2kbd: irq enabled with vector %u\n", isr->id & 0xff);
}
