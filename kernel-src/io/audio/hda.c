#include <kernel/pci.h>
#include <logging.h>
#include <kernel/hda.h>

static void initcontroller(pcienum_t *e) {
	//pcibar_t bar0p = pci_getbar(e, 0);
	//volatile nvmebar0_t *bar0 = (volatile nvmebar0_t *)bar0p.address;
	//__assert(bar0);

	pci_setcommand(e, PCI_COMMAND_MMIO, 1);
	pci_setcommand(e, PCI_COMMAND_IO, 0);
	pci_setcommand(e, PCI_COMMAND_IRQDISABLE, 1);
	pci_setcommand(e, PCI_COMMAND_BUSMASTER, 1);

	printf("hda: found controller at %02x:%02x.%x\n", e->bus, e->device, e->function);

	// enable interrupts

	size_t intcount;
	if (e->msix.exists) {
		intcount = pci_initmsix(e);
	} else if (e->msi.exists) {
	//	intcount = pci_initmsi(e);
	} else {
		printf("hda: controller doesn't support msi-x or msi\n");
		return;
	}
}

void hda_init() {
	int i = 0;
	for (;;) {
		pcienum_t *e = pci_getenum(-1, -1, -1, 0x8086, 0x2668, -1, i++);
		if (e == NULL)
			break;
		initcontroller(e);
	}
}
