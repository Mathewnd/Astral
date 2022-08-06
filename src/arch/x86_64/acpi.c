#include <arch/acpi.h>
#include <limine.h>
#include <stdbool.h>
#include <stdio.h>
#include <arch/panic.h>
#include <string.h>
#include <kernel/pmm.h>

static bool  usersdt = false;
static sdt_t* sdt;
static size_t sdtentrycount;


static volatile struct limine_rsdp_request rsdpreq = {
	.id = LIMINE_RSDP_REQUEST,
	.revision = 0
};

bool acpi_checksumok(sdt_t* table){
	uint8_t sum;
	uint8_t* ptr = (uint8_t*)table;
	
	for(size_t i = 0; i < table->length; ++i)
		sum += *ptr++;
	return sum == 0;
}

void* acpi_gettable(char* sig, size_t n){
	size_t ptrsize = usersdt ? 4 : 8;
	
	void* ptr = (char*)sdt + sizeof(sdt_t);
	

	for(size_t i = 0; i < sdtentrycount; ++i){
		
		sdt_t* table = usersdt ? (*(uint32_t*)ptr & 0xFFFFFFFF) : *(uint64_t*)ptr;
		table = (uint8_t*)table + (size_t)limine_hhdm_offset;

		if(!strncmp(sig, table->signature, 4)){
			if(n-- == 0)
				return table;
				
		}
		
		ptr += ptrsize;

	}

	return NULL;
	
}

void acpi_init(){
	if(!rsdpreq.response){
		// TODO search for ourselves
		_panic("No reponse for the RSDP request", 0);
	}

	rsdp_t* rsdp = rsdpreq.response->address;

	printf("RSDP at: %p\n", rsdp);
	
	sdt = rsdp->xsdtaddr;

	if(rsdp->revision == 0){
		printf("WARNING: XSDT does not exist in revision 0, falling back to RSDT!\n");
		sdt = rsdp->rsdtaddr;
		usersdt = true;
	}
	
	sdt = (uint8_t*)sdt + (size_t)limine_hhdm_offset;

	sdtentrycount = (sdt->length - sizeof(sdt_t)) / (usersdt ? 4 : 8);

	printf("Root System Description Table address: %p with %lu tables\n", sdt, sdtentrycount);

	if(!acpi_checksumok(sdt))
		_panic("Invalid RSDT checksum!", 0);

}
