#ifndef _ACPI_H_INCLUDE
#define _ACPI_H_INCLUDE

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
	char signature[8];
	uint8_t checksum;
	char oem[6];
	uint8_t revision;
	uint32_t rsdtaddr;
	
	uint32_t length;
	uint64_t xsdtaddr;
	uint8_t  xchecksum;
} __attribute__((packed))rsdp_t;

typedef struct {
	char signature[4];
	uint32_t length;
	uint8_t  revision;
	uint8_t  checksum;
	char oem[6];
	char oemtable[8];
	uint32_t oemrevision;
	uint32_t creator;
	uint32_t creatorrevision;
} __attribute__((packed)) sdt_t;

typedef struct {
	sdt_t header;
	uint64_t pointers[];
} __attribute__((packed)) xsdt_t;

typedef struct {
	sdt_t header;
	uint32_t pointers[];
} __attribute__((packed)) rsdt_t;

typedef struct {
	sdt_t header;
	uint32_t apicaddr;
	uint32_t flags;
} __attribute__((packed)) madt_t;

typedef struct {
	uint64_t address;
	uint16_t segmentgroup;
	uint8_t  startbus;
	uint8_t  endbus;
	uint32_t reserved;

} __attribute__((packed)) mcfgentry;

typedef struct {
	sdt_t header;
	uint64_t reserved;
	mcfgentry entries[];
} __attribute__((packed)) mcfg_t;


typedef struct {
	sdt_t header;
	uint8_t revid;
	uint8_t flags;
	uint16_t vendor;
	uint8_t spacetype;
	uint8_t bitwidth;
	uint8_t bitoffset;
	uint8_t reserved;
	uint64_t address;
	uint8_t hpetnum;
	uint16_t mintick;
	uint8_t protection;
} __attribute__((packed)) hpet_t;

void* acpi_gettable(char* sig, size_t n);
void  acpi_init();
bool  acpi_checksumok(sdt_t* table);

#endif
