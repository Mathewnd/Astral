#ifndef _PS2_H_INCLUDE
#define _PS2_H_INCLUDE

#include <arch/timekeeper.h>
#include <arch/io.h>

#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define PS2_PORT_DATA 0x60
#define PS2_PORT_COMMAND 0x64
#define PS2_PORT_STATUS 0x64
#define RESEND 0xFE
#define ACK 0xFA
#define ECHO 0xEE
#define KEYBOARDIRQ 1
#define MOUSEIRQ 12

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
#define DEVICE_CMD_IDENTIFY 0xF2

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
			*timeout = true;
			return 0;
		}
	}
	
	*timeout = false;
	return read_data();
	
}

static inline uint8_t device_write_response(int port, uint8_t command){
	uint8_t r = RESEND;
	int tries = 0;

	while(r == RESEND && tries < 5){
		device_command(port, command);
		r = read_data();
		++tries;
	}
	
	return r;

}

#endif
