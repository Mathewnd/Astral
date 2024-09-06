#include <arch/ps2.h>
#include <logging.h>
#include <arch/ps2kbd.h>
#include <arch/ps2mouse.h>

static void ps2_disablescanning(int port) {
	ps2_device_command(port, PS2_DEVICE_CMD_DISABLESCANNING);

	int resendtries = 0;

	// loop until timeout, resend if we need to
	// this is done to clear the buffer and make sure no more data will be read
	for (;;) {
		bool timeout;
		uint8_t data = ps2_read_data_timeout(100, &timeout);
		if (timeout)
			break;

		if (data == PS2_RESEND) {
			if (resendtries++ > 5) {
				printf("ps2: ps2_disablescanning: port %d: too many resends\n", port);
				return;
			}

			ps2_device_command(port, PS2_DEVICE_CMD_DISABLESCANNING);
		}
	}

	// just to make sure
	ps2_flush_outbuffer();
}

static bool ps2_enablescanning(int port) {
	ps2_device_command(port, PS2_DEVICE_CMD_ENABLESCANNING);
	int resendtries = 0;

	for (;;) {
		bool timeout;
		uint8_t data = ps2_read_data_timeout(100, &timeout);
		if (timeout) {
			printf("ps2: ps2_enablescanning: port %d: timeout\n", port);
			return false;
		}

		if (data == PS2_ACK)
			break;

		if (data == PS2_RESEND) {
			if (resendtries++ > 5) {
				printf("ps2: ps2_enablescanning: port %d: too many resends\n", port);
				return false;
			}
		}
	}

	// just to make sure
	ps2_flush_outbuffer();
	return true;
}

static bool ps2_device_reset_selftest(int port) {
	ps2_disablescanning(port);

	ps2_device_command(port, PS2_DEVICE_CMD_RESETSELFTEST);
	int resendtries = 0;
	// loop until ACK or timeout, RESEND if needed
	// this will make sure the reset command is ACK'd by the device
	for (;;) {
		bool timeout;
		uint8_t data = ps2_read_data_timeout(500, &timeout);

		if (timeout) {
			printf("ps2: ps2_reset_selftest: port %d: timed out waiting for ACK\n", port);
			return false;
		}

		if (data == PS2_RESEND) {
			if (resendtries > 5) {
				printf("ps2: ps2_reset_selftest: port %d: too many resend tries for self test command\n", port);
				return false;
			}

			ps2_device_command(port, PS2_DEVICE_CMD_RESETSELFTEST);
			++resendtries;
			continue;
		}

		if (data == PS2_ACK)
			break;
	}

	// now wait for the reset to be done
	for (;;) {
		bool timeout;
		uint8_t data = ps2_read_data_timeout(2000, &timeout);
		if (timeout) {
			printf("ps2: ps2_reset_selftest: port %d: timed out waiting for result\n", port);
			return false;
		}

		if (data == PS2_DEVICE_SELFTEST_OK)
			break;

		if (data == PS2_DEVICE_SELFTEST_FAIL) {
			printf("ps2: ps2_reset_selftest: port %d: self test failed\n");
			return false;
		}
	}

	// and clear whatever could come our way
	for (;;) {
		bool timeout;
		ps2_read_data_timeout(100, &timeout);
		if (timeout)
			break;
	}

	// just to make sure
	ps2_flush_outbuffer();
	return true;
}

bool ps2_init_device(int port, bool *mouse) {
	if (ps2_device_reset_selftest(port) == false)
		return false;

	// to make sure it is still disabled
	ps2_disablescanning(port);
	return true;
}

void arch_ps2_init() {
	// XXX actually check if the controller is there
	ps2_write_command(PS2_CTLR_CMD_DISABLEP1);
	ps2_write_command(PS2_CTLR_CMD_DISABLEP2);

	// just to make sure
	ps2_flush_outbuffer();

	ps2_write_command(PS2_CTLR_CMD_READCFG);
	uint8_t control = ps2_read_data();

	bool dualport = control & PS2_CONFIG_CLOCKP2;

	control &= ~(PS2_CONFIG_IRQP1 | PS2_CONFIG_IRQP2 | PS2_CONFIG_TRANSLATION);

	ps2_write_command(PS2_CTLR_CMD_WRITECFG);
	ps2_write_data(control);

	ps2_write_command(PS2_CTLR_CMD_SELFTEST);
	uint8_t result = ps2_read_data();

	if (result != PS2_CTLR_SELFTEST_OK) {
		printf("ps2: controller self test failed (expected %x got %x)\n", PS2_CTLR_SELFTEST_OK, result);
		return;
	}

	ps2_write_command(PS2_CTLR_CMD_WRITECFG);
	ps2_write_data(control); // restore config if it was reset

	// confirm existance of second port
	if (dualport) {
		ps2_write_command(PS2_CTLR_CMD_ENABLEP2);
		ps2_write_command(PS2_CTLR_CMD_READCFG);
		control = ps2_read_data();

		if (dualport && (control & PS2_CONFIG_CLOCKP2)) // not enabled, not dual port.
			dualport = false;
		else
			ps2_write_command(PS2_CTLR_CMD_DISABLEP2); // disable second port again
	}

	// do port self tests
	int workingflag = 0;

	ps2_write_command(PS2_CTLR_CMD_P1SELFTEST);
	result = ps2_read_data();
	if (result == 0)
		workingflag |= 1;
	else
		printf("ps2: first PS/2 port self test failed! Expected 0 got %x\n", result);

	if (dualport) {
		ps2_write_command(PS2_CTLR_CMD_P2SELFTEST);
		result = ps2_read_data();
		if (result == 0)
			workingflag |= 2;
		else
			printf("ps2: second port self test failed! Expected 0 got %x\n", result);
	}

	if (workingflag == 0) {
		printf("ps2: no working ports\n");
		return;
	}

	printf("ps2: controller with %d ports\n", dualport ? 2 : 1);

	// reset and self test devices
	// the ports leave this with scanning disabled

	int connectedflag = 0;

	if (workingflag & 1) {
		if (ps2_device_reset_selftest(1) == false) {
			printf("ps2: device self test for port 1 failed\n");
		} else {
			connectedflag |= 1;
		}
	}

	if (workingflag & 2) {
		if (ps2_device_reset_selftest(2) == false) {
			printf("ps2: device self test for port 2 failed\n");
		} else {
			connectedflag |= 2;
		}
	}

	if (connectedflag == 0) {
		printf("ps2: no working devices\n");
		return;
	}

	uint8_t okflag = 0;
	// now initialize and enable the device stuff
	if (connectedflag & 1) {
		ps2_write_command(PS2_CTLR_CMD_ENABLEP1);
		// figure out what the device is TODO and actually use the result for something
		uint8_t identity[2];
		if (ps2_identify(1, identity) == false) {
			printf("ps2: failed to identify first port\n");
			goto do_2;
		} else {
			ps2kbd_init();
			ps2_write_command(PS2_CTLR_CMD_DISABLEP2);
			okflag |= 1;
		}
	}

	do_2:
	if (connectedflag & 2) {
		ps2_write_command(PS2_CTLR_CMD_ENABLEP2);
		// figure out what the device is TODO and actually use the result for something
		uint8_t identity[2];
		if (ps2_identify(1, identity) == false) {
			printf("ps2: failed to identify second port\n");
		} else {
			ps2mouse_init();
			ps2_write_command(PS2_CTLR_CMD_DISABLEP2);
			okflag |= 2;
		}
	}

	if (okflag & 1) {
		ps2_write_command(PS2_CTLR_CMD_ENABLEP1);
		if (ps2_enablescanning(1) == false) {
			printf("ps2: failed to enable scanning for port 1\n");
		}
	}

	if (okflag & 2) {
		ps2_write_command(PS2_CTLR_CMD_ENABLEP2);
		if (ps2_enablescanning(2) == false) {
			printf("ps2: failed to enable scanning for port 2\n");
		}
	}

	// enable irqs and translation in config
	ps2_write_command(PS2_CTLR_CMD_READCFG);
	control = ps2_read_data();

	control |= (dualport ? PS2_CONFIG_IRQP1 | PS2_CONFIG_IRQP2 : PS2_CONFIG_IRQP1) | PS2_CONFIG_TRANSLATION;

	ps2_write_command(PS2_CTLR_CMD_WRITECFG);
	ps2_write_data(control);
}
