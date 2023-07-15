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
	pci_archwrite32(bus, device, function, offset, value);
}

int pci_getcapoffset(pcienum_t *e, int cap, int n) {
	if ((pci_read16(e->bus, e->device, e->function, PCI_CONFIG_STATUS) & PCI_STATUS_HASCAP) == 0)
		return 0;

	int offset = pci_read8(e->bus, e->device, e->function, PCI_CONFIG_CAP);
	while (offset) {
		if (pci_read8(e->bus, e->device, e->function, offset) == cap) {
			if (n-- == 0)
				break;
		}

		offset = pci_read8(e->bus, e->device, e->function, offset + 1);
	}

	return offset;
}

#define BAR_IO 1
#define BAR_TYPE_MASK 0x6
#define BAR_TYPE_64BIT 0x4

pcibar_t pci_getbar(pcienum_t *e, int n) {
	// TODO map bars (also taking in count the prefetchable flag)
	pcibar_t ret;
	int offset = 0x10 + n * 4;

	uint32_t bar = pci_read32(e->bus, e->device, e->function, offset);
	if (bar & BAR_IO) {
		ret.mmio = false;
		ret.address = bar & 0xfffffffc;
		pci_write32(e->bus, e->device, e->function, offset, -1);
		ret.length = ~(pci_read32(e->bus, e->device, e->function, offset) & 0xfffffffc) + 1;
		pci_write32(e->bus, e->device, e->function, offset, bar);
	} else {
		ret.mmio = true;
		ret.address = bar & 0xfffffff0;
		ret.length = -1;
		if ((bar & BAR_TYPE_MASK) == BAR_TYPE_64BIT)
			ret.address = (uint64_t)pci_read32(e->bus, e->device, e->function, offset + 4) << 32;

		pci_write32(e->bus, e->device, e->function, offset, -1);
		ret.length = ~(pci_read32(e->bus, e->device, e->function, offset) & 0xfffffff0) + 1;
		pci_write32(e->bus, e->device, e->function, offset, bar);
	}

	return ret;
}

static pcienum_t *enums;

pcienum_t *pci_getenum(int class, int subclass, int progif, int vendor, int deviceid, int revision, int n) {
	pcienum_t *e;
	for (e = enums; e; e = e->next) {
		if (
		(class == -1 	|| e->class 	== class) 	&&
		(subclass == -1 || e->subclass 	== subclass) 	&&
		(progif == -1 	|| e->progif 	== progif) 	&&
		(vendor == -1 	|| e->vendor 	== vendor) 	&&
		(deviceid == -1 || e->deviceid 	== deviceid) 	&&
		(revision == -1 || e->revision 	== revision)) {
			if (n-- == 0)
				break;
		}
	}

	return e;
}

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

	int offset = pci_getcapoffset(e, PCI_CAP_MSIX, 0);
	if (offset) {
		e->msix.exists = true;
		e->msix.offset = offset;
	}

	offset = pci_getcapoffset(e, PCI_CAP_MSI, 0);
	if (offset) {
		e->msi.exists = true;
		e->msi.offset = offset;
	}

	char *irqtype = e->msix.exists ? "msi-x" : (e->msi.exists ? "msi" : "no interrupts");
	printf("pci: found %02x:%02x.%x %02x%02x: %04x:%04x (rev %2x) (%s)\n", bus, device, function, e->class, e->subclass, e->vendor, e->device, e->revision, irqtype);
}

static void enumeratedevice(int bus, int device) {
	bool multifunction = pci_read8(bus, device, 0, PCI_CONFIG_HEADERTYPE) & PCI_HEADERTYPE_MULTIFUNCTION_MASK;
	size_t testcount = multifunction ? 8 : 1;

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
