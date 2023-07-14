#ifndef _PCI_H
#define _PCI_H

// 2 byte fields
#define PCI_CONFIG_VENDOR 0x0
#define PCI_CONFIG_DEVICEID 0x2

// 1 byte fields
#define PCI_CONFIG_REVISION 0x8
#define PCI_CONFIG_PROGIF 0x9
#define PCI_CONFIG_SUBCLASS 0xa
#define PCI_CONFIG_CLASS 0xb
#define PCI_CONFIG_HEADERTYPE 0xe

#define PCI_HEADERTYPE_MULTIFUNCTION_MASK 0x80
#define PCI_HEADERTYPE_MASK 0x7f
#define PCI_HEADERTYPE_STANDARD 0

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
} pcienum_t;

uint8_t pci_read8(int bus, int device, int function, uint32_t offset);
uint16_t pci_read16(int bus, int device, int function, uint32_t offset);
uint32_t pci_read32(int bus, int device, int function, uint32_t offset);
void pci_write8(int bus, int device, int function, uint32_t offset, uint8_t v);
void pci_write16(int bus, int device, int function, uint32_t offset, uint16_t v);
void pci_write32(int bus, int device, int function, uint32_t offset, uint32_t value);
void pci_init();

#endif
