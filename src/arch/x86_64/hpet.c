#include <arch/hpet.h>
#include <arch/acpi.h>
#include <arch/pci.h>
#include <arch/panic.h>
#include <kernel/vmm.h>
#include <arch/mmu.h>
#include <stdio.h>

#define REGISTER_CAPABILITIES 0
#define REGISTER_CONFIGURATION 0x10
#define REGISTER_INTERRUPT 0x20
#define REGISTER_COUNTER 0xF0
#define REGISTER_N_CONFIG 0x100
#define REGISTER_N_COMPARATOR 0x108
#define REGISTER_N_FSB 0x110

static uint16_t mintick;
static void* volatile hpetaddr;
static uint32_t period;
static uint64_t nsperiod;
static uint8_t  compcount;
static uint64_t ticksper100ns;

static inline void writetocompn(size_t n, size_t offset, uint64_t data){
	*(uint64_t* volatile)(hpetaddr + 0x20*n + offset) = data;
}

static inline uint64_t readfromcompn(size_t n, size_t offset){
	return *(uint64_t* volatile)(hpetaddr + 0x100 + 0x20*n + offset);
}

static inline uint64_t readfromreg(size_t offset){
	return *(uint64_t* volatile)(hpetaddr + offset);
}

static inline void writetoreg(size_t offset, uint64_t data){
	*(uint64_t* volatile)(hpetaddr + offset) = data;
}

static void wait_ticks(size_t ticks){

	size_t end = readfromreg(REGISTER_COUNTER) + ticks;
	
	while(readfromreg(REGISTER_COUNTER) < end) asm("pause");
}

time_t hpet_get_ticksperus(){
	return ticksper100ns*10;
}

time_t hpet_get_counter(){
	return readfromreg(REGISTER_COUNTER);
}

void hpet_wait_us(size_t us){

	size_t usticks = ticksper100ns*10;
	
	wait_ticks(usticks*us);
	
}

void hpet_wait_ms(size_t ms){
	size_t msticks = ticksper100ns*10000;
	
	wait_ticks(ms*msticks);

}

void hpet_wait_s(size_t s){
	size_t sticks = ticksper100ns*10000000;

	wait_ticks(s*sticks);

}

void hpet_init(){
	
	hpet_t* hpet = acpi_gettable("HPET", 0);

	if(!hpet)
		_panic("No HPET found!\n", 0);

	printf("HPET table at %p\n", hpet);

	mintick = hpet->mintick;
	
	if(hpet->spacetype == 1) _panic("I/O space hpet not supported", 0);

	printf("HPET configuration space is at %p\n", hpet->address);
	
	// seabios doesn't report this in the memory map.....
	
	hpetaddr = vmm_alloc(1, 0);
	vmm_map((void*)hpet->address, hpetaddr, 1, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE);
	
	uint64_t capabilities = readfromreg(REGISTER_CAPABILITIES);
	
	compcount = (capabilities >> 8) & 0b11111;
	period = (capabilities >> 32) & 0xFFFFFFFF;
	nsperiod = period / 1000000;
	ticksper100ns = 100000000 / period;

	printf("%lu comparators at %lu femtosecond ticks (%lu ns, %lu ticks per 100 ns)\n", compcount, period, nsperiod, ticksper100ns);
	
	// configure so we don't use legacy replacement and clear the counter
	
	writetoreg(REGISTER_CONFIGURATION, 0); // stop it
	writetoreg(REGISTER_COUNTER, 0);
	writetoreg(REGISTER_CONFIGURATION, 1); // turn it back on

	// TODO add interrupt support. for now only direct reads to the 
	// counter will be supported.
	// (this should be enough for calibrating the CPU specific timers)
	
}
