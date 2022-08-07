#ifndef _PCI_H_INCLUDE
#define _PCI_H_INCLUDE

#include <stdint.h>
#include <stddef.h>

#define PCI_DEV_HEADER 0
#define PCI_BRIDGE_HEADER 1
#define PCI_CARDBUS_HEADER 2

#define PCI_CARDBUS_HEADERSIZE 0x48
#define PCI_OTHER_HEADERSIZE 0x40

typedef struct{
	uint16_t vendor;
	uint16_t device;
	uint16_t command;
	uint16_t status;
	uint8_t  revision;
	uint8_t  progif;
	uint8_t  subclass;
	uint8_t  class;
	uint8_t  cachesize;
	uint8_t  latencytimer;
	uint8_t  type;
	uint8_t  bist;
} __attribute__((packed)) pci_common;

typedef struct{
	pci_common common;
	uint32_t BAR[6];
	uint32_t cardbuscis;
	uint16_t subsystemvendor;
	uint16_t subsystem;
	uint32_t rombase;
	uint8_t  capabilities;
	uint8_t  r[3];
	uint32_t reserved;
	uint8_t  interruptline;
	uint8_t  interruptpin;
	uint8_t  mingrant;
	uint8_t  maxgrant;
} __attribute__((packed)) pci_deviceheader;

typedef struct _pci_enumeration{
	struct pci_enumeration* next;
	uint8_t bus;
	uint8_t device;
	uint8_t function;
	pci_common* header;
} pci_enumeration;

uint64_t pci_msi_build(uint64_t* data, uint8_t vector, uint8_t processor, uint8_t edgetrigger, uint8_t deassert);
void pci_enumerate();

#endif
