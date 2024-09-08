#ifndef _PS2_H
#define _PS2_H

#include <kernel/timekeeper.h>
#include <arch/io.h>

#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define PS2_PORT_DATA 0x60
#define PS2_PORT_COMMAND 0x64
#define PS2_PORT_STATUS 0x64
#define PS2_RESEND 0xfe
#define PS2_ACK 0xfa
#define PS2_ECHO 0xee
#define PS2_KEYBOARDIRQ 1
#define PS2_MOUSEIRQ 12

#define PS2_CTLR_CMD_SECONDPORTSELECT 0xd4
#define PS2_CTLR_CMD_DISABLEP1 0xad
#define PS2_CTLR_CMD_DISABLEP2 0xa7
#define PS2_CTLR_CMD_READCFG 0x20
#define PS2_CTLR_CMD_WRITECFG 0x60
#define PS2_CTLR_CMD_SELFTEST 0xaa
#define PS2_CTLR_CMD_ENABLEP1 0xae
#define PS2_CTLR_CMD_ENABLEP2 0xa8	
#define PS2_CTLR_CMD_P1SELFTEST 0xab
#define PS2_CTLR_CMD_P2SELFTEST 0xa9

#define PS2_CTLR_SELFTEST_OK 0x55

#define PS2_CONFIG_IRQP1 1
#define PS2_CONFIG_IRQP2 2
#define PS2_CONFIG_TRANSLATION 0x40
#define PS2_CONFIG_CLOCKP2 0x20

#define PS2_DEVICE_CMD_RESETSELFTEST 0xff
#define PS2_DEVICE_CMD_DISABLESCANNING 0xf5
#define PS2_DEVICE_CMD_ENABLESCANNING 0xf4
#define PS2_DEVICE_CMD_IDENTIFY 0xf2
#define PS2_DEVICE_SELFTEST_OK 0xaa
#define PS2_DEVICE_SELFTEST_FAIL 0xfc

// needs to be waited to read
static inline bool ps2_outbuffer_empty() {
	return (inb(PS2_PORT_STATUS) & 1) == 0;
}

// needs to be waited to write
static inline bool ps2_inbuffer_full() {
	return (inb(PS2_PORT_STATUS) & 2) == 2;
}

static inline void ps2_flush_outbuffer() {
	while (ps2_outbuffer_empty() == false)
		inb(PS2_PORT_DATA);
}

static inline void ps2_write_command(uint8_t cmd) {
	while (ps2_inbuffer_full());
	outb(PS2_PORT_COMMAND, cmd);
}

static inline void ps2_write_data(uint8_t cmd) {
	while (ps2_inbuffer_full());
	outb(PS2_PORT_DATA, cmd);
}

static inline void ps2_device_command(int port, uint8_t command) {
	if (port == 2)
		ps2_write_command(PS2_CTLR_CMD_SECONDPORTSELECT);

	ps2_write_data(command);
}

static inline uint8_t ps2_read_data() {
	while (ps2_outbuffer_empty());

	return inb(PS2_PORT_DATA);
}

static inline uint8_t ps2_read_data_timeout(int ms, bool *timeout) {
	timespec_t initial = timekeeper_timefromboot();

	while (ps2_outbuffer_empty()) {
		timespec_t current = timekeeper_timefromboot();
		if (timespec_diffms(initial, current) >= ms) {
			*timeout = true;
			return 0;
		}
	}

	*timeout = false;
	return ps2_read_data();
}

static inline bool ps2_device_write_ok(int port, uint8_t command) {
	ps2_device_command(port, command);
	int resends = 0;
	bool timeout;

	for (;;) {
		uint8_t data = ps2_read_data_timeout(100, &timeout);
		if (timeout)
			return false;

		if (data == PS2_RESEND) {
			if (resends++ > 5)
				return false;

			ps2_device_command(port, command);
		}

		if (data == PS2_ACK)
			break;
	}

	return true;
}

static inline uint8_t ps2_device_write_response(int port, uint8_t command) {
	uint8_t r = PS2_RESEND;
	int tries = 0;

	while (r == PS2_RESEND && tries < 5) {
		ps2_device_command(port, command);
		r = ps2_read_data();
		++tries;
	}

	return r;
}

static inline bool ps2_identify(int port, uint8_t results[2]) {
	ps2_device_command(port, PS2_DEVICE_CMD_IDENTIFY);
	int resends = 0;
	bool timeout;

	for (;;) {
		uint8_t data = ps2_read_data_timeout(100, &timeout);
		if (timeout)
			return false;

		if (data == PS2_RESEND) {
			if (resends++ > 5)
				return false;

			ps2_device_command(port, PS2_DEVICE_CMD_IDENTIFY);
		}

		if (data == PS2_ACK)
			break;
	}

	// now read the actual data bytes
	results[0] = ps2_read_data_timeout(100, &timeout);
	if (timeout)
		return false;

	// secon read can timeout just fine.
	results[1] = ps2_read_data_timeout(100, &timeout);
	return true;
}

void arch_ps2_init();

#endif
