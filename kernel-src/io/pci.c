#include <logging.h>
#include <arch/pci.h>
#include <kernel/pci.h>
#include <kernel/alloc.h>
#include <kernel/vmm.h>

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

void pci_setcommand(pcienum_t *e, int mask, int v) {
	uint16_t current = pci_read16(e->bus, e->device, e->function, PCI_CONFIG_COMMAND);

	if (v)
		current |= mask;
	else
		current &= ~mask;

	pci_write16(e->bus, e->device, e->function, PCI_CONFIG_COMMAND, current);
}

typedef struct {
	uint32_t addresslow;
	uint32_t addresshigh;
	uint32_t data;
	uint32_t control;
} __attribute__((packed)) msixmessage_t;

void pci_msixadd(pcienum_t *e, int msixvec, int vec, int edgetrigger, int deassert) {
	__assert(e->msix.exists);
	__assert(msixvec < e->irq.msix.entrycount);

	pcibar_t bir = pci_getbar(e, e->irq.msix.bir);

	volatile msixmessage_t *table = (msixmessage_t *)(bir.address + e->irq.msix.tableoffset);
	uint32_t data;
	uint64_t address = msiformatmessage(&data, vec, edgetrigger, deassert);

	table[msixvec].addresslow = address & 0xffffffff;
	table[msixvec].addresshigh = (address >> 32) & 0xffffffff;
	table[msixvec].data = data;
	table[msixvec].control = 0;
}

void pci_msixsetmask(pcienum_t *e, int v) {
	uint16_t msgctl = pci_read16(e->bus, e->device, e->function, e->msix.offset + 2);
	msgctl = v ? msgctl | 0x4000 : msgctl & ~0x4000;
	pci_write16(e->bus, e->device, e->function, e->msix.offset + 2, msgctl);
}

size_t pci_initmsix(pcienum_t *e) {
	if (e->msix.exists == false)
		return 0;

	uint16_t msgctl = pci_read16(e->bus, e->device, e->function, e->msix.offset + 2);
	msgctl |= 0xc000;
	pci_write16(e->bus, e->device, e->function, e->msix.offset + 2, msgctl);

	e->irq.msix.entrycount = (msgctl & 0x3ff) + 1;

	uint32_t bir = pci_read32(e->bus, e->device, e->function, e->msix.offset + 4);
	e->irq.msix.bir = bir & 0x7;
	e->irq.msix.tableoffset = bir & ~0x7;

	bir = pci_read32(e->bus, e->device, e->function, e->msix.offset + 8);
	e->irq.msix.pbir = bir & 0x7;
	e->irq.msix.pboffset = bir & ~0x7;

	pci_setcommand(e, PCI_COMMAND_IRQDISABLE, 1);

	return e->irq.msix.entrycount;
}

#define BAR_MAP_FLAGS (ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_NOEXEC)

void *pci_mapbar(pcibar_t bar) {
	__assert(bar.mmio);
	return vmm_map(NULL, bar.length, VMM_FLAGS_PHYSICAL, BAR_MAP_FLAGS | (bar.prefetchable ? ARCH_MMU_FLAGS_WT : ARCH_MMU_FLAGS_UC), (void *)bar.address);
}

#define BAR_IO 1
#define BAR_TYPE_MASK 0x6
#define BAR_TYPE_64BIT 0x4
#define BAR_PREFETCHABLE 0x8

pcibar_t pci_getbar(pcienum_t *e, int n) {
	if (e->bar[n].length)
		return e->bar[n];

	pcibar_t ret;
	int offset = 0x10 + n * 4;

	uint32_t bar = pci_read32(e->bus, e->device, e->function, offset);
	if (bar & BAR_IO) {
		ret.mmio = false;
		ret.prefetchable = false;
		ret.address = bar & 0xfffffffc;
		pci_write32(e->bus, e->device, e->function, offset, -1);
		ret.length = ~(pci_read32(e->bus, e->device, e->function, offset) & 0xfffffffc) + 1;
		pci_write32(e->bus, e->device, e->function, offset, bar);
	} else {
		ret.mmio = true;
		ret.prefetchable = bar & BAR_PREFETCHABLE;
		ret.address = bar & 0xfffffff0;
		ret.length = -1;
		if ((bar & BAR_TYPE_MASK) == BAR_TYPE_64BIT)
			ret.address += (uint64_t)pci_read32(e->bus, e->device, e->function, offset + 4) << 32;

		pci_write32(e->bus, e->device, e->function, offset, -1);
		ret.length = ~(pci_read32(e->bus, e->device, e->function, offset) & 0xfffffff0) + 1;
		pci_write32(e->bus, e->device, e->function, offset, bar);
		ret.address = (uint64_t)pci_mapbar(ret);
		__assert(ret.address);
	}

	e->bar[n] = ret;
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
