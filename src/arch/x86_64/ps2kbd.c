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

static uint8_t ps2cmd(uint8_t cmd){
	
	uint8_t response;

	for(size_t i = 0; i < 3; ++i){
		outb(PS2_PORT_DATA, cmd);
		response = inb(PS2_PORT_DATA);
		
		if(response != RESEND)
			break;

	}

	return response;

}

static char asciitable[] = {
	0, '\033', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', 0, 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '4', '1', '2', '3', '0', '.', 0, 0, 0, 0, 0
};

void ps2kbd_irq(){
	uint8_t scancode = inb(0x60);
	if(scancode == 0xE0)
		return; // TODO support extended scancodes
	
	kbpacket_t packet;
	
	// TODO convert scancode to keycode

	packet.flags = 0;

	if(scancode & 0x80){
		packet.flags = KBPACKET_FLAGS_RELEASED;
		scancode &= 0x7F;
	}

	packet.ascii = asciitable[scancode];

	keyboard_packet(kb, &packet);

}

void ps2kbd_init(){
	
	// maybe the ps/2 controller and keyboard should be initialised to a known state
	// and not be assumed to be set up in a working manner by the firmware/bootloader
	
	// this probably shouldn't assume the keyboard always being in the first port but oh well
	
	// clear keyboard buffer data

	inb(PS2_PORT_DATA);
	
	if(ps2cmd(ECHO) != ECHO){
		printf("No PS/2 keyboard found.\n");
		return;
	}
	
	ioapic_setlegacyirq(KEYBOARDIRQ, VECTOR_PS2KBD, arch_getcls()->lapicid, false);
	
	kb = keyboard_getnew();
}
