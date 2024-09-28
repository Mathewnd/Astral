#include <uacpi/kernel_api.h>
#include <uacpi/status.h>
#include <uacpi/types.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include <uacpi/sleep.h>
#include <uacpi/event.h>
#include <kernel/acpi.h>
#include <kernel/interrupt.h>
#include <logging.h>

void acpi_early_init(void) {
	__assert(uacpi_initialize(0) == UACPI_STATUS_OK);
}

void acpi_dopoweroff(uacpi_handle ctx) {
	(void)ctx;
	acpi_signaldevice('p');
}

static uacpi_interrupt_ret handle_pwrbtn(uacpi_handle ctx) {
	uacpi_kernel_schedule_work(UACPI_WORK_GPE_EXECUTION, acpi_dopoweroff, ctx);
	return UACPI_INTERRUPT_HANDLED;
}

void acpi_init(void) {
	uacpi_status ret;

	ret = uacpi_namespace_load();
	__assert(ret == UACPI_STATUS_OK);

	uacpi_set_interrupt_model(UACPI_INTERRUPT_MODEL_IOAPIC);

	ret = uacpi_namespace_initialize();
	__assert(ret == UACPI_STATUS_OK);

	uacpi_install_fixed_event_handler(UACPI_FIXED_EVENT_POWER_BUTTON, handle_pwrbtn, UACPI_NULL);
}

int acpi_poweroff(void) {
	uacpi_status ret = uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);
	if (uacpi_unlikely_error(ret)) {
		printf("acpi: unable to prepare for sleep: %s", uacpi_status_to_string(ret));
		return EIO;
	}

	interrupt_set(false);

	ret = uacpi_enter_sleep_state(UACPI_SLEEP_STATE_S5);
	if (uacpi_unlikely_error(ret)) {
		interrupt_set(true);
		printf("acpi: unable to enter S5: %s", uacpi_status_to_string(ret));
		return EIO;
	}

	return 0;
}

int acpi_reboot(void) {
	/*
	 * Windows does reboot via shutdown, so some hardware expects \_PTS(5)
	 * to be called prior to rebooting, do that.
	 */
	uacpi_prepare_for_sleep_state(UACPI_SLEEP_STATE_S5);

	uacpi_status ret = uacpi_reboot();
	if (uacpi_unlikely_error(ret)) {
		printf("acpi: unable to perform system reset: %s\n", uacpi_status_to_string(ret));
		return EIO;
	}

	return 0;
}
