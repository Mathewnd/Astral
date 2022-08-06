#include <arch/apic.h>
#include <arch/acpi.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <arch/panic.h>
#include <stdio.h>
#include <arch/cls.h>

#define APIC_REGISTER_ID 0x20
#define APIC_REGISTER_EOI 0xB0
#define APIC_REGISTER_SPURIOUS 0xF0


#define TYPE_LAPIC 0
#define TYPE_IOAPIC 1
#define TYPE_OVERRIDE 2
#define TYPE_IOAPICNMI 3
#define TYPE_LAPICNMI 4
#define TYPE_ADDROVERRIDE 5
#define TYPE_X2APIC 9

typedef struct{
	uint8_t type;
	uint8_t length;
} __attribute__((packed)) entry_head;

typedef struct{
	entry_head header;
	uint8_t acpi_id;
	uint8_t apicid;
	uint32_t flags;
} __attribute__((packed)) lapic_entry;

typedef struct{
	entry_head header;
	uint8_t id;
	uint8_t reserved;
	uint32_t address;
	uint32_t gsibase;
} __attribute__((packed)) ioapic_entry;

typedef struct{
	entry_head header;
	uint8_t bus;
	uint8_t irq;
	uint32_t gsi;
	uint16_t flags;

} __attribute__((packed)) override_entry;

typedef struct{
	entry_head header;
	uint8_t source;
	uint8_t reserved;
	uint16_t flags;
	uint32_t gsi;
} __attribute__((packed)) ioapicnmi_entry;

typedef struct{
	entry_head header;
	uint8_t acpi_id;
	uint16_t flags;
	uint8_t lint;
} __attribute__((packed)) lapicnmi_entry;

typedef struct{
	entry_head header;
	uint16_t reserved;
	uint64_t addr;
} __attribute__((packed)) overrideaddr_entry;

typedef struct{
	entry_head header;
	uint16_t reserved;
	uint32_t x2apicid;
	uint32_t flags;
	uint32_t acpi_id;
} __attribute__((packed)) x2apic_entry;

typedef struct{
	int id;
	int gsi;
	volatile void* address;
	size_t mre;
} ioapic_descriptor;

static size_t lapic_count = 0;
static size_t ioapic_count = 0;
static size_t override_count = 0;
static size_t ionmi_count = 0;
static size_t localnmi_count = 0;
static size_t x2apic_count = 0;

void* lapicaddr;
ioapic_descriptor* ioapics;

void*  tablestart;
void*  tableend;

static inline entry_head* next(entry_head* entry){
	return (void*)entry + entry->length;
}

static void* findstructure(size_t type, size_t n){
		
	entry_head* entry = tablestart;


	while(entry < tableend){	
		if(entry->type == type){
			if(n-- == 0)
				return entry;
		}

		entry = next(entry);
	}

	return NULL;

}

static inline void ioapic_writereg(ioapic_descriptor* ioapic, uint32_t offset, uint32_t data){
	volatile uint32_t* reg = ioapic->address;
	*reg = offset; // select
	*(reg + 4) = data;  // data
}

static inline uint32_t ioapic_readreg(ioapic_descriptor* ioapic, uint32_t offset){
	volatile uint32_t* reg = ioapic->address;
	*reg = offset; // select
	return *(reg + 4); // data


}

static void ioapic_writeiored(ioapic_descriptor* ioapic, uint8_t irq, uint8_t vector, uint8_t delivery, uint8_t destmode, uint8_t polarity, uint8_t irr, uint8_t mode, uint8_t mask, uint8_t dest){
	uint32_t val = vector;
	val |= (delivery & 0b111) << 8;
	val |= (destmode & 1) << 11;
	val |= (polarity & 1) << 13;
	val |= (irr & 1) << 14;
	val |= (mode & 1) << 15;
	val |= (mask & 1) << 16;
	
	ioapic_writereg(ioapic, 0x10 + irq * 2, val & 0xFFFFFFFF);
	ioapic_writereg(ioapic, 0x10 + irq * 2 + 1, dest << 24);

}

static inline uint32_t lapic_readreg(size_t reg){
	return *(uint32_t* volatile)(lapicaddr + reg);
}

static inline void lapic_writereg(size_t reg, uint32_t data){
	*(uint32_t* volatile)(lapicaddr + reg) = data;
}

void apic_lapicinit(){
	
	arch_getcls()->lapicid = (lapic_readreg(APIC_REGISTER_ID));
	arch_getcls()->lapicid >>= 24;
	
	lapic_writereg(APIC_REGISTER_SPURIOUS, 0x1FF);
	
}

void apic_init(){
	
	// first get some cool info about the apic in the system

	madt_t* apic = acpi_gettable("APIC", 0);
	
	if(!apic)
		_panic("No MADT found", 0);

	printf("MADT at %p\n", apic);
	
	lapicaddr = apic->apicaddr;

	// get the count of relevant entries
	
	size_t len = apic->header.length - sizeof(madt_t);
	void*  start = (void*)apic + sizeof(madt_t);
	void*  end = start + len;
	entry_head* entry = start;
	tablestart = start;
	tableend   = end;
	while(entry < end){	
		switch(entry->type){
			case TYPE_LAPIC:
				++lapic_count;
				break;
			case TYPE_IOAPIC:
				++ioapic_count;
				break;
			case TYPE_OVERRIDE:
				++override_count;
				break;
			case TYPE_IOAPICNMI:
				++ionmi_count;
				break;
			case TYPE_LAPICNMI:
				++localnmi_count;
				break;
			case TYPE_ADDROVERRIDE:
				lapicaddr = ((overrideaddr_entry*)entry)->addr;
				break;
			case TYPE_X2APIC:
				++x2apic_count;
				break;

		}
		entry = next(entry);
	}

	printf("MADT info:\nLocal APIC count: %lu\nIO APIC count: %lu\noverride count: %lu\nIO NMI count: %lu\nLocal NMI count: %lu\nX2APIC count: %lu\nLocal apic address: %p\n", lapic_count, ioapic_count, override_count, ionmi_count, localnmi_count, x2apic_count, lapicaddr);
	
	// map the local apic into memory (sea bios doesn't pass this to the limine map for some reason? I know my laptop UEFI firmware passes it)
	
	void* tmp = vmm_alloc(1, 0);
	if(!tmp) _panic("Out of memory", 0);
	vmm_map(lapicaddr, tmp, 1, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE);
	lapicaddr = tmp;

	// now set up the IO apic

	ioapics = alloc(sizeof(ioapic_descriptor)*ioapic_count);

	for(size_t i = 0; i < ioapic_count; ++i){
		
		ioapic_entry* entry = findstructure(TYPE_IOAPIC, i);
		void* tmp = vmm_alloc(1, 0);
		if(!tmp) _panic("Out of memory", 0);
		vmm_map(entry->address, tmp, 1, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE);
		
		ioapics[i].address = tmp;
		ioapics[i].gsi = entry->gsibase;
		ioapics[i].id  = entry->id;
		ioapics[i].mre = (ioapic_readreg(&ioapics[i], 1) >> 16) & 0xFF;
		
		for(size_t j = 0; j <= ioapics[i].mre; ++j)
			ioapic_writeiored(&ioapics[i], j, 0, 0, 0, 0, 0, 0, 1, 0);
	}



}
