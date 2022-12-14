#include <arch/ps2kbd.h>
#include <arch/io.h>
#include <stdbool.h>
#include <arch/cls.h>
#include <arch/idt.h>
#include <arch/apic.h>
#include <kernel/keyboard.h>

#define PS2_PORT_DATA 0x60
#define PS2_PORT_COMMAND 0x64
#define PS2_PORT_STATUS 0x64
#define RESEND 0xFE
#define ACK 0xFA
#define ECHO 0xEE
#define KEYBOARDIRQ 1

static int kb;

static char codes[] = {
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

static bool extended = false;

void ps2kbd_irq(){
	uint8_t scancode = inb(0x60);
	if(scancode == 0xE0){
		extended = true;
		return;
	}
	
	kbpacket_t packet;
	
	packet.flags = 0;

	if(scancode & 0x80){
		packet.flags = KBPACKET_FLAGS_RELEASED;
		scancode &= 0x7F;
	}

	char *tab = codes;

	if(extended){
		tab = extendedcodes;
		extended = false;
	}

	packet.keycode = tab[scancode];

	if(!packet.keycode)
		return;

	keyboard_packet(kb, packet);

}

void ps2kbd_init(){
		
	// clear PS/2 buffer to make sure its empty

	inb(PS2_PORT_DATA);
	
	ioapic_setlegacyirq(KEYBOARDIRQ, VECTOR_PS2KBD, arch_getcls()->lapicid, false);
	
	kb = keyboard_getnew();
}
