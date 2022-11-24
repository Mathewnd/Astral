#include <arch/pci.h>
#include <arch/acpi.h>
#include <arch/io.h>
#include <kernel/alloc.h>
#include <stdio.h>

pci_enumeration* enumerations;

#define CONFIG_ADDRESS 0xCF8
#define CONFIG_DATA 0xCFC

#define COMMAND_REG 1
#define CAP_MSI 5

pci_enumeration* pci_getdevicecs(int class, int subclass, int n){
	
	pci_enumeration* ptr = enumerations;
	pci_enumeration*  save = NULL;

	while(true){
		while(ptr){
			
			pci_common* common = ptr->header;
			if((common->type & 0x7F) == PCI_DEV_HEADER && common->class == class && common->subclass == subclass){
				save = ptr;
				if(ptr->next == NULL)
					break;
				ptr = ptr->next;
				break;
				
			}

			ptr = ptr->next;
		}

		if(!ptr)
			break;

		if(n-- == 0)
			break;
	}

	if(n >= 0)
		save = NULL;

	return save;

}
pci_enumeration* pci_getdevicecsp(int class, int subclass, int progif, int n){
	pci_enumeration* ptr = enumerations;
	pci_enumeration*  save = NULL;

	while(ptr){
		while(ptr){
			
			pci_common* common = ptr->header;

			if((common->type & 0x7F) == PCI_DEV_HEADER && common->class == class && common->subclass == subclass && common->progif == progif){
				save = ptr;
				ptr = ptr->next;
				break;
				
			}

			ptr = ptr->next;
		}

		if(n-- == 0)
			break;

	}

	if(n < 0)
		save = NULL;

	return save;
}

static uint32_t legacy_readdata(int bus, int device, int func, int reg){
	
	uint32_t address = reg << 2;
	address |= (func & 0b111) << 8;
	address |= (device & 0b11111) << 11;
	address |= (bus & 0xFF) << 16;
	address |= 1 << 31;
	
	outd(CONFIG_ADDRESS, address);

	return ind(CONFIG_DATA);

}

static void legacy_writedata(int bus, int device, int func, int reg, uint32_t val){
	
	uint32_t address = reg << 2;
        address |= (func & 0b111) << 8;
        address |= (device & 0b11111) << 11;
        address |= (bus & 0xFF) << 16;
        address |= 1 << 31;

        outd(CONFIG_ADDRESS, address);
	
	outd(CONFIG_DATA, val);

}

void pci_setcommand(pci_enumeration* e, int which, int val){

	uint32_t regsave = legacy_readdata(e->bus, e->device, e->function, COMMAND_REG);

	if(val == 0){
		regsave &= ~which;
	}
	else{
		regsave |= which;
	}
	
	legacy_writedata(e->bus, e->device, e->function, COMMAND_REG, regsave);

}

static inline bool legacy_exists(int bus, int device, int func){
	return legacy_readdata(bus, device, func, 0) != 0xFFFFFFFF;
}

static pci_enumeration* allocnewenum(){

	pci_enumeration* enumeration;

	if(!enumerations){
		enumeration = alloc(sizeof(pci_enumeration));
		enumerations = enumeration;
	}
	else{
		enumeration = enumerations;
		while(enumeration->next != NULL)
			enumeration = enumeration->next;
		
		enumeration->next = alloc(sizeof(pci_enumeration));
		enumeration = enumeration->next;
	}

	return enumeration;
}

static void legacy_copyheader(pci_enumeration* dev, size_t size){
	size_t count = size / 4;
	uint32_t* where = (uint32_t*)dev->header;

	for(size_t i = 0; i < count; ++i)
		*where++ = legacy_readdata(dev->bus, dev->device, dev->function, i);
	
}



static size_t legacy_getcapabilityoffset(size_t id, pci_enumeration* enumeration){
	
	// does this device have a capability list

	if(enumeration->header->status & (1 << 4) == 0) return 0;
	
	size_t offset = ((pci_deviceheader*)enumeration->header)->capabilities>> 2;
	
	while(offset){
		uint32_t word = legacy_readdata(enumeration->bus, enumeration->device, enumeration->function, offset);

		size_t targetid = word & 0xFF;

		if(targetid == id)
			return offset;

		offset = (word >> 8) & 0xFF;
		offset >>= 2;

	}
	
	return 0;

}

bool pci_msisupport(pci_enumeration* e){
	if(!legacy_getcapabilityoffset(CAP_MSI, e))
		return false;

	return true;
}

void pci_msienable(pci_enumeration* e, bool enable){
	
	int offset = legacy_getcapabilityoffset(CAP_MSI, e);

	if(!offset)
		return;

	uint32_t currval = legacy_readdata(e->bus, e->device, e->function, offset);

	if((currval & 1 << 23) == 0)
		_panic("32 bit MSI not supported!");

	if(enable){
		currval |= 1 << 16;
	}
	else{
		currval &= ~(1 << 16);
	}

	legacy_writedata(e->bus, e->device, e->function, offset, currval);

}

void pci_msimask(pci_enumeration* e, int which, int val){
	int offset = legacy_getcapabilityoffset(CAP_MSI, e);

        if(!offset)
                return;

	offset += 2;

        uint32_t currval = legacy_readdata(e->bus, e->device, e->function, offset);
	
	if(val){
		currval |= 1 << which;
	}
	else{
		currval &= ~(1 << which);
	}

	legacy_writedata(e->bus, e->device, e->function, offset, currval);

}

void pci_msimaskall(pci_enumeration* e, int val){
	
	int offset = legacy_getcapabilityoffset(CAP_MSI, e);

        if(!offset)
                return;

        offset += 2;

        uint32_t currval = legacy_readdata(e->bus, e->device, e->function, offset);
        
        if(val){
                currval = 0xFFFFFFFF;
        }
        else{
                currval = 0;
        }

        legacy_writedata(e->bus, e->device, e->function, offset, currval);

}

static void legacy_enumeratedevice(int bus, int device){
	
	// if the device is multifunction, scan and enumerate all 8. else, only one

	size_t functioncount = ((legacy_readdata(bus, device, 0, 3) >> 16) & 0xFF) != 0 ? 8 : 1;
	
	for(size_t function = 0; function < functioncount; ++function){
		if(!legacy_exists(bus, device, function)) 
			continue;

		pci_enumeration* enumeration = allocnewenum();
		
		enumeration->bus = bus;
		enumeration->device = device;
		enumeration->function = function;
		
		// type with multifunction masked off
		size_t type = (legacy_readdata(bus, device, function, 3) >> 16) & 0x7F;
		
		size_t headersize = type == PCI_CARDBUS_HEADER ? PCI_CARDBUS_HEADERSIZE : PCI_OTHER_HEADERSIZE;

		enumeration->header = alloc(headersize);

		legacy_copyheader(enumeration, headersize);	
		
		printf("PCI: %02x:%02x.%x %02x%02x: %04x:%04x (rev %2x) \n", bus, device, function, enumeration->header->class, enumeration->header->subclass, enumeration->header->vendor, enumeration->header->device, enumeration->header->revision);
	}

}

static void legacy_enumeration(){
	
	for(size_t bus = 0; bus < 1; ++bus){
		for(size_t device = 0; device < 32; ++device){
			if(legacy_exists(bus, device, 0))
				legacy_enumeratedevice(bus, device);
		}
	}
	
}

uint64_t pci_msi_build(uint64_t* data, uint8_t vector, uint8_t processor, uint8_t edgetrigger, uint8_t deassert){
	*data = vector | (edgetrigger << 15) | (deassert << 14);
	return (0xFEE00000 | (processor << 12));
}

void pci_enumerate(){
	
	// TODO PCIe enumeration	
	
	legacy_enumeration();

}
