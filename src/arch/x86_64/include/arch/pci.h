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

#define PCI_TYPE_MEM 1
#define PCI_MEM_64 2

// bit 0 set if mem; bit 1 set if 64 bit (in case of mem)

static inline int pci_bartype(uint32_t bar){
	
	if(bar & 1)
		return 0;
	

	if(((bar >> 1) & 3) == 2)
		return 3;

	return 1;

}

static inline void* getbarmemaddr(pci_deviceheader* e, int bar){

	int type = pci_bartype(e->BAR[bar]);

	if(type == 0)
		return NULL;

	if(type & PCI_MEM_64)
		return (e->BAR[bar] & ~(0xF)) + ((uint64_t)e->BAR[bar + 1] << 32);
	else
		return e->BAR[bar] & ~(0xF);
}

pci_enumeration* pci_getdevicecs(int class, int subclass, int n);
pci_enumeration* pci_getdevicecsp(int class, int subclass, int progif, int n);
uint64_t pci_msi_build(uint64_t* data, uint8_t vector, uint8_t processor, uint8_t edgetrigger, uint8_t deassert);
void pci_enumerate();

#endif
