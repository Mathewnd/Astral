#include <arch/ps2.h>
#include <printf.h>
#include <arch/ps2kbd.h>

static bool reset_selftest(int port) {
	uint8_t r = RESEND;
	bool timeout = false;
	int attempts = 0;

	while (r == RESEND && attempts < 10) {
		device_command(port, DEVICE_CMD_RESETSELFTEST);
		r = read_data_timeout(5, &timeout);
	}

	if (attempts == 10)
		timeout = true;

	if (!timeout) {
		if (read_data() != DEVICE_SELFTEST_OK)
			printf("ps2: %s port device self test failed!\n", port == 1 ? "First" : "Second");
		else
			return true;
	}

	return false;
}

void arch_ps2_init() {
	// XXX actually check if the controller is there
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
	if (read_data() != CTLR_SELFTEST_OK)
		printf("ps2: controller self test failed.\n");

	write_command(CTLR_CMD_WRITECFG);
	write_data(control); // restore config if it was reset

	// confirm existance of second port

	write_command(CTLR_CMD_ENABLEP2);

	write_command(CTLR_CMD_READCFG);
	control = read_data();

	if (dualport && (control & CONFIG_CLOCKP2)) // not enabled, not dual port.
		dualport = false;
	else
		write_command(CTLR_CMD_DISABLEP2); // disable second port again

	// self test ports
	int workingflag = 0;

	write_command(CTLR_CMD_P1SELFTEST);
	if (read_data() == 0)
		workingflag |= 1;
	else
		printf("First PS/2 port self test failed!\n");

	if (dualport) {
		write_command(CTLR_CMD_P2SELFTEST);
		if (read_data() == 0)
			workingflag |= 2;
		else
			printf("ps2: econd port self test failed!\n");
	}

	if (workingflag == 0) {
		printf("ps2: no working ports.\n");
		return;
	}

	printf("ps2: controller with %d ports\n", dualport ? 2 : 1);

	// enable ports

	write_command(CTLR_CMD_ENABLEP1);
	if (dualport)
		write_command(CTLR_CMD_ENABLEP2);

	// enable irqs and translation in config

	write_command(CTLR_CMD_READCFG);
	control = read_data();

	control |= (dualport ? CONFIG_IRQP1 | CONFIG_IRQP2 : CONFIG_IRQP1) | CONFIG_TRANSLATION;

	write_command(CTLR_CMD_WRITECFG);
	write_data(control);

	// reset and self test devices

	int connectedflag = 0;

	if (workingflag & 1 && reset_selftest(1))
		connectedflag |= 1;

	if (workingflag & 2 && reset_selftest(2))
		connectedflag |= 2;

	// XXX should id each one for proper identification
	if (connectedflag & 1)
		ps2kbd_init();
}
