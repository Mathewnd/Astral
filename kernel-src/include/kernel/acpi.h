#ifndef _ACPI_H
#define _ACPI_H

// Doesn't depend on PCI, only makes table search available
void acpi_early_init(void);

// Loads the actual ACPI namespace, hooks up the power button
void acpi_init(void);

int acpi_poweroff(void);
int acpi_reboot(void);

void acpi_signaldevice(char c);

// Depends on devfs
void acpi_initdevice(void);

#endif
