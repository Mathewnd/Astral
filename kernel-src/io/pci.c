#include <logging.h>
#include <arch/pci.h>
#include <kernel/pci.h>
#include <kernel/alloc.h>

uint8_t pci_read8(int bus, int device, int function, uint32_t offset) {
	return (pci_archread32(bus, device, function, offset) >> ((offset & 0b11) * 8)) & 0xff;
}

uint16_t pci_read16(int bus, int device, int function, uint32_t offset) {
	return (pci_archread32(bus, device, function, offset) >> ((offset & 0b11) * 8)) & 0xffff;
}

void pci_write8(int bus, int device, int function, uint32_t offset, uint8_t v) {
	uint32_t old = pci_archread32(bus, device, function, offset);
	int bitoffset = 8 * (offset & 0b11);
	old &= ~(0xff << bitoffset);
	old |= v << bitoffset;
	pci_archwrite32(bus, device, function, offset, old);
}

void pci_write16(int bus, int device, int function, uint32_t offset, uint16_t v) {
	uint32_t old = pci_archread32(bus, device, function, offset);
	int bitoffset = 8 * (offset & 0b11);
	old &= ~(0xffff << bitoffset);
	old |= v << bitoffset;
	pci_archwrite32(bus, device, function, offset, old);
}

uint32_t pci_read32(int bus, int device, int function, uint32_t offset) {
	return pci_archread32(bus, device, function, offset);
}
void pci_write32(int bus, int device, int function, uint32_t offset, uint32_t value) {
	return pci_archwrite32(bus, device, function, offset, value);
}

static pcienum_t *enums;

// reads all 1s if a function doesn't exist
#define DOESNTEXIST(bus, device, function) (pci_read32(bus, device, function, 0) == 0xffffffff)

static void enumeratefunction(int bus, int device, int function) {
	pcienum_t *e = alloc(sizeof(pcienum_t));
	__assert(e);
	e->bus = bus;
	e->device = device;
	e->function = function;
	e->next = enums;
	e->class = pci_read8(bus, device, function, PCI_CONFIG_CLASS);
	e->subclass = pci_read8(bus, device, function, PCI_CONFIG_SUBCLASS);
	e->progif = pci_read8(bus, device, function, PCI_CONFIG_PROGIF);
	e->vendor = pci_read16(bus, device, function, PCI_CONFIG_VENDOR);
	e->deviceid = pci_read16(bus, device, function, PCI_CONFIG_DEVICEID);
	e->revision = pci_read8(bus, device, function, PCI_CONFIG_REVISION);
	enums = e;

	printf("pci: found %02x:%02x.%x %02x%02x: %04x:%04x (rev %2x) \n", bus, device, function, e->class, e->subclass, e->vendor, e->device, e->revision);
}

static void enumeratedevice(int bus, int device) {
	bool multifunction = pci_read32(bus, device, 0, PCI_CONFIG_HEADERTYPE) & PCI_HEADERTYPE_MULTIFUNCTION_MASK;
	size_t testcount = multifunction ? 1 : 8;

	for (int i = 0; i < testcount; ++i) {
		if (DOESNTEXIST(bus, device, i))
			continue;
		enumeratefunction(bus, device, i);
	}
}

void pci_init() {
	pci_archinit();
	for (size_t bus = 0; bus < 1; ++bus) {
		for (size_t device = 0; device < 32; ++device) {
			if (DOESNTEXIST(bus, device, 0))
				continue;
			enumeratedevice(bus, device);
		}
	}
}
