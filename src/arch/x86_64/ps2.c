#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <arch/io.h>
#include <arch/acpi.h>
#include <stdio.h>

#include <time.h>
#include <arch/timekeeper.h>
#include <arch/ps2kbd.h>

#define PS2_PORT_DATA 0x60
#define PS2_PORT_COMMAND 0x64
#define PS2_PORT_STATUS 0x64
#define RESEND 0xFE
#define ACK 0xFA
#define ECHO 0xEE
#define KEYBOARDIRQ 1

#define CTLR_CMD_SECONDPORTSELECT 0xD4

#define CTLR_CMD_DISABLEP1 0xAD
#define CTLR_CMD_DISABLEP2 0xA7

#define CTLR_CMD_READCFG 0x20
#define CTLR_CMD_WRITECFG 0x60
#define CTLR_CMD_SELFTEST 0xAA
#define CTLR_CMD_ENABLEP1 0xAE
#define CTLR_CMD_ENABLEP2 0xA8	
#define CTLR_CMD_P1SELFTEST 0xAB
#define CTLR_CMD_P2SELFTEST 0xA9

#define CTLR_SELFTEST_OK 0x55

#define CONFIG_IRQP1 1
#define CONFIG_IRQP2 2
#define CONFIG_TRANSLATION 0x40
#define CONFIG_CLOCKP2 0x20

#define DEVICE_CMD_RESETSELFTEST 0xFF

#define SELFTEST_OK 0xAA

static inline bool inbuffer_full(){
	return (inb(PS2_PORT_STATUS) & 2) == 2;
}

static inline bool outbuffer_full(){
	return (inb(PS2_PORT_STATUS) & 1) == 1;
}

static inline void write_command(uint8_t cmd){
	while(inbuffer_full());
	outb(PS2_PORT_COMMAND, cmd);
}

static inline void write_data(uint8_t cmd){
	while(inbuffer_full());
	outb(PS2_PORT_DATA, cmd);
}

static inline void device_command(int port, uint8_t command){
	if(port == 2)
		write_command(CTLR_CMD_SECONDPORTSELECT);

	write_data(command);

}

static inline uint8_t read_data(){
	while(!outbuffer_full());
	return inb(PS2_PORT_DATA);
}

static inline uint8_t read_data_timeout(int ms, bool* timeout){
	
	struct timespec initial = arch_timekeeper_gettimefromboot();

	while(outbuffer_full() == false){
		struct timespec current = arch_timekeeper_gettimefromboot();
		if(time_diffms(initial, current) >= ms){
			printf("spinning!!! diff: %d\n", time_diffms(initial, current));
			*timeout = true;
			return 0;
		}
	}
	
	*timeout = false;
	return read_data();
	
}

static bool reset_selftest(int port){
	uint8_t r = RESEND;
	bool timeout = false;

	int attempts = 0;

	while(r == RESEND && attempts < 10){
		
		device_command(port, DEVICE_CMD_RESETSELFTEST);

		r = read_data_timeout(5, &timeout);

		++attempts;
	}

	if(attempts >= 10)
		timeout = true;
	
	if(!timeout){ // device connected

		// check self test results

		if(read_data() != SELFTEST_OK)
			printf("%s PS/2 port device self test failed!\n", port == 1 ? "First" : "Second");
		else
			return true;
	}

	return false;
}



void ps2_init(){
	
	// XXX actually check if the controller is there

	// disable devices

	write_command(CTLR_CMD_DISABLEP1);
	write_command(CTLR_CMD_DISABLEP2);

	inb(PS2_PORT_DATA); // flush output buffer
	
	write_command(CTLR_CMD_READCFG);
	uint8_t control = read_data();

	bool dualport = control & CONFIG_CLOCKP2;
	
	control &= ~(CONFIG_IRQP1 | CONFIG_IRQP2 | CONFIG_TRANSLATION);

	write_command(CTLR_CMD_WRITECFG);
	write_data(control);

	write_command(CTLR_CMD_SELFTEST);
	if(read_data() != CTLR_SELFTEST_OK){
		printf("PS/2 controller self test failed.\n");
	}

	write_command(CTLR_CMD_WRITECFG);
	write_data(control); // restore config if it was reset

	// confirm existance of second port
	
	write_command(CTLR_CMD_ENABLEP2);
	
	write_command(CTLR_CMD_READCFG);
	control = read_data();

	if(dualport && (control & CONFIG_CLOCKP2)) // not enabled, not dual port.
		dualport = false;
	else
		write_command(CTLR_CMD_DISABLEP2); // disable second port again

	// self test ports

	int workingflag = 0;


	write_command(CTLR_CMD_P1SELFTEST);
	if(read_data() == 0)
		workingflag |= 1;
	else
		printf("First PS/2 port self test failed!\n");

	if(dualport){
		write_command(CTLR_CMD_P2SELFTEST);
		if(read_data() == 0)
			workingflag |= 2;
		else
			printf("Second PS/2 port self test failed!\n");
	}
		
	if(workingflag == 0){
		printf("No working PS/2 ports.\n");
		return;
	}
	
	printf("PS2 controller with %d ports found\n", dualport ? 2 : 1);
	
	
	write_command(CTLR_CMD_ENABLEP1);

	if(dualport)
		write_command(CTLR_CMD_ENABLEP2);

	// enable irqs and translation in config
	
	write_command(CTLR_CMD_READCFG);
	control = read_data();

	control |= (dualport ? CONFIG_IRQP1 | CONFIG_IRQP2 : CONFIG_IRQP1) | CONFIG_TRANSLATION;
	
	write_command(CTLR_CMD_WRITECFG);
	write_data(control);
	
	// reset and self test devices
	
	int connectedflag = 0;

	if(workingflag & 1 && reset_selftest(1))
		connectedflag |= 1;

	if(workingflag & 2 && reset_selftest(2))
		connectedflag |= 2;

	// XXX don't hardcode first port as keyboard and second port as mouse

	if(workingflag & 1)
		ps2kbd_init();

}
