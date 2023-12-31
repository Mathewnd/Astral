#ifndef _PCI_H
#define _PCI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PCI_BAR_BASE 0x10

// 2 byte fields
#define PCI_CONFIG_VENDOR 0x0
#define PCI_CONFIG_DEVICEID 0x2
#define PCI_CONFIG_COMMAND 0x4
	#define PCI_COMMAND_IO 0x1
	#define PCI_COMMAND_MMIO 0x2
	#define PCI_COMMAND_IRQDISABLE 0x400
#define PCI_CONFIG_STATUS 0x6
	#define PCI_STATUS_HASCAP 0x10

// 1 byte fields
#define PCI_CONFIG_REVISION 0x8
#define PCI_CONFIG_PROGIF 0x9
#define PCI_CONFIG_SUBCLASS 0xa
	#define PCI_SUBCLASS_STORAGE_NVM 0x8
#define PCI_CONFIG_CLASS 0xb
	#define PCI_CLASS_STORAGE 0x1
#define PCI_CONFIG_HEADERTYPE 0xe
	#define PCI_HEADERTYPE_MULTIFUNCTION_MASK 0x80
	#define PCI_HEADERTYPE_MASK 0x7f
	#define PCI_HEADERTYPE_STANDARD 0
#define PCI_CONFIG_CAP 0x34
	#define PCI_CAP_MSI 0x5
	#define PCI_CAP_VENDORSPECIFIC 0x9
	#define PCI_CAP_MSIX 0x11

typedef struct {
	bool exists;
	int offset;
} pcicap_t;

typedef struct {
	uintptr_t address;
	uintptr_t physical;
	size_t length;
	bool mmio;
	bool is64bits;
	bool prefetchable;
} pcibar_t;

typedef struct pcienum_t {
	struct pcienum_t *next;
	int bus;
	int device;
	int function;
	int type;
	int class;
	int subclass;
	int progif;
	int vendor;
	int deviceid;
	int revision;
	pcicap_t msi;
	pcicap_t msix;
	pcibar_t bar[6];
	union {
		struct {
			int bir;
			uintmax_t tableoffset;
			int pbir;
			uintmax_t pboffset;
			size_t entrycount;
		} msix;
	} irq;
} pcienum_t;

uint8_t pci_read8(int bus, int device, int function, uint32_t offset);
uint16_t pci_read16(int bus, int device, int function, uint32_t offset);
uint32_t pci_read32(int bus, int device, int function, uint32_t offset);
void pci_write8(int bus, int device, int function, uint32_t offset, uint8_t v);
void pci_write16(int bus, int device, int function, uint32_t offset, uint16_t v);
void pci_write32(int bus, int device, int function, uint32_t offset, uint32_t value);
pcienum_t *pci_getenum(int class, int subclass, int progif, int vendor, int deviceid, int revision, int n);
int pci_getcapoffset(pcienum_t *e, int cap, int n);
pcibar_t pci_getbar(pcienum_t *e, int bar);
void *pci_mapbar(pcibar_t bar);
void pci_setcommand(pcienum_t *e, int mask, int v);
size_t pci_initmsix(pcienum_t *e);
void pci_msixsetmask(pcienum_t *e, int v);
void pci_msixadd(pcienum_t *e, int msixvec, int vec, int edgetrigger, int deassert);
void pci_init();

#define PCI_READ32(e, offset) pci_read32(e->bus, e->device, e->function, offset)
#define PCI_READ16(e, offset) pci_read16(e->bus, e->device, e->function, offset)
#define PCI_READ8(e, offset) pci_read8(e->bus, e->device, e->function, offset)
#define PCI_WRITE32(e, offset, value) pci_write32(e->bus, e->device, e->function, offset, value)
#define PCI_WRITE16(e, offset, value) pci_write16(e->bus, e->device, e->function, offset, value)
#define PCI_WRITE8(e, offset, value) pci_write8(e->bus, e->device, e->function, offset, value)


#endif
