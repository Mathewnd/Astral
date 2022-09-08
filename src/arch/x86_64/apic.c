#include <arch/apic.h>
#include <arch/acpi.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <arch/panic.h>
#include <stdio.h>
#include <arch/cls.h>
#include <arch/hpet.h>

#define APIC_REGISTER_ID 0x20
#define APIC_REGISTER_EOI 0xB0
#define APIC_REGISTER_SPURIOUS 0xF0
#define APIC_REGISTER_ICR_LO 0x300
#define APIC_REGISTER_ICR_HI 0x310
#define APIC_TIMER_LVT 0x320
#define APIC_THERMAL_LVT 0x330
#define APIC_PERFORMANCE_LVT 0x340
#define APIC_LINT0_LVT 0x350
#define APIC_LINT1_LVT 0x360
#define APIC_ERROR_LVT 0x370
#define APIC_TIMER_INITIALCOUNT 0x380
#define APIC_TIMER_COUNT 0x390
#define APIC_TIMER_DIVIDE 0x3E0

#define LVT_DELIVERY_NMI (0b100 << 8)
#define LVT_MASK (1 << 16)

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

static void* lapicaddr;
static bool  done = false;
static ioapic_descriptor* ioapics;

static void*  tablestart;
static void*  tableend;

static ioapic_descriptor* ioapicforgsi(uint8_t gsi){
	
	ioapic_descriptor* ioapic = ioapics;

	for(uintmax_t i = 0; i < ioapic_count; ++i){
		if(ioapics[i].gsi > ioapic->gsi)
			ioapic = &ioapics[i];
	}

	return ioapic;

}

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

static void ioapic_writeiored(ioapic_descriptor* ioapic, uint8_t irq, uint8_t vector, uint8_t delivery, uint8_t destmode, uint8_t polarity, uint8_t mode, uint8_t mask, uint8_t dest){
	uint32_t val = vector;
	val |= (delivery & 0b111) << 8;
	val |= (destmode & 1) << 11;
	val |= (polarity & 1) << 13;
	val |= (mode & 1) << 15;
	val |= (mask & 1) << 16;
	
	ioapic_writereg(ioapic, 0x10 + irq * 2, val & 0xFFFFFFFF);
	ioapic_writereg(ioapic, 0x10 + irq * 2 + 1, dest << 24);

}

void ioapic_setlegacyirq(uint8_t irq, uint8_t vector, uint8_t proc, bool masked){
	
	// default settings for ISA irqs
	
	uint8_t polarity = 1;
	uint8_t trigger  = 0;
	
	for(uintmax_t i = 0; i < override_count; ++i){
		override_entry* override = findstructure(TYPE_OVERRIDE, i);
		
		if(override->irq != irq)
			continue;
		
		polarity = override->flags & 2 ? 1 : 0; // active low
		trigger  = override->flags & 8 ? 1 : 0; // level triggered
		irq = override->irq;


		break;


	}

	ioapic_descriptor* ioapic = ioapicforgsi(irq);

	irq = irq - ioapic->gsi;

	ioapic_writeiored(ioapic, irq, vector, 0, 0, polarity, trigger, masked ? 1 : 0, proc);
	
}

static inline uint32_t lapic_readreg(size_t reg){
	return *(uint32_t volatile*)(lapicaddr + reg);
}

static inline void lapic_writereg(size_t reg, uint32_t data){
	*(uint32_t volatile*)(lapicaddr + reg) = data;
}

void apic_sendipi(uint8_t cpu, uint8_t vec, uint8_t dest, uint8_t mode, uint8_t level){

	if(!done) return; // this function will be called before the apic initialization code is run so we need this safeguard

	lapic_writereg(APIC_REGISTER_ICR_HI, (uint32_t)cpu << 24);
	lapic_writereg(APIC_REGISTER_ICR_LO, vec | (level << 14) | (mode << 15) | (dest << 18));
}

void apic_eoi(){
	lapic_writereg(APIC_REGISTER_EOI, 0);
}

void apic_timerstart(size_t ticks){
	lapic_writereg(APIC_TIMER_INITIALCOUNT, ticks);
}

void apic_timerinterruptset(uint8_t vector){
	lapic_writereg(APIC_TIMER_LVT, vector);
}

size_t apic_timerstop(){

	size_t ticksremaining = lapic_readreg(APIC_TIMER_COUNT);

	lapic_writereg(APIC_TIMER_INITIALCOUNT, 0);

	return ticksremaining;
}

size_t apic_timercalibrate(size_t us){

	lapic_writereg(APIC_TIMER_INITIALCOUNT, 0xFFFFFFFF);
	lapic_writereg(APIC_TIMER_DIVIDE, 0b111); // no divider

	hpet_wait_ms(us);

	size_t ticks = 0xFFFFFFFF - lapic_readreg(APIC_TIMER_COUNT);

	apic_timerstop();

	return ticks / 1000;

}

void apic_lapicinit(){
	
	// mask everything off besides the timer
	
	lapic_writereg(APIC_THERMAL_LVT, LVT_MASK);
	lapic_writereg(APIC_PERFORMANCE_LVT, LVT_MASK);
	lapic_writereg(APIC_LINT0_LVT, LVT_MASK);
	lapic_writereg(APIC_LINT1_LVT, LVT_MASK);
	lapic_writereg(APIC_ERROR_LVT, LVT_MASK);


	arch_getcls()->lapicid = (lapic_readreg(APIC_REGISTER_ID));
	arch_getcls()->lapicid >>= 24;

	lapic_entry* current;
	// get the acpi id for the local nmi sources
	
	for(size_t i = 0; i < lapic_count; ++i){
		current = findstructure(TYPE_LAPIC, i);
		if(arch_getcls()->lapicid == current->apicid)
			break;
	}

	arch_getcls()->acpi_id = current->acpi_id;

	lapic_writereg(APIC_REGISTER_SPURIOUS, 0x1FF);
	lapic_writereg(APIC_TIMER_LVT, 1 << 16); // mask timer interrupt off

	for(size_t i = 0; i < localnmi_count; ++i){
		lapicnmi_entry* e = findstructure(TYPE_LAPICNMI, i);
		if(e->acpi_id == current->acpi_id || e->acpi_id == 0xFF){
			lapic_writereg(APIC_LINT0_LVT + 0x10*e->lint, 0x30 | LVT_DELIVERY_NMI | (e->flags << 12));
		}
	}

	asm("sti");

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
			ioapic_writeiored(&ioapics[i], j, 0, 0, 0, 0, 0, 1, 0);
	}

	done = true;

}
