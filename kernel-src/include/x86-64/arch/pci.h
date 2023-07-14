#ifdef _PCIARCH_H
#error this file should only be included ONCE and only in the io/pci.c file
#else
#define _PCIARCH_H

#include <stdint.h>
#include <stddef.h>
#include <arch/io.h>
#include <arch/acpi.h>
#include <kernel/pmm.h>

static size_t mcfgentrycount;
static mcfgentry_t *mcfgentries;
static uint32_t (*pci_archread32)(int bus, int device, int function, uint32_t offset);
static void (*pci_archwrite32)(int bus, int device, int function, uint32_t offset, uint32_t value);

#define CONFADD 0xcf8
#define CONFDATA 0xcfc

static uint32_t legacy_read32(int bus, int device, int function, uint32_t offset) {
	uint32_t confadd = 0x80000000 | (offset & ~0x3) | (function << 8) | (device << 11) | (bus << 16);
	outd(CONFADD, confadd);
	return ind(CONFDATA);
}

static void legacy_write32(int bus, int device, int function, uint32_t offset, uint32_t value) {
	uint32_t confadd = 0x80000000 | (offset & ~0x3) | (function << 8) | (device << 11) | (bus << 16);
	outd(CONFADD, confadd);
	outd(CONFDATA, value);
}

static inline mcfgentry_t *getmcfgentry(int bus) {
	mcfgentry_t *entry;
	int i;

	for (i = 0; i < mcfgentrycount; ++i) {
		entry = &mcfgentries[i];
		if (bus >= entry->startbus && bus <= entry->endbus)
			break;
	}

	if (i == mcfgentrycount)
		entry = NULL;

	return entry;
}

static uint32_t mcfg_read32(int bus, int device, int function, uint32_t offset) {
	mcfgentry_t *entry = getmcfgentry(bus);
	if (entry == NULL)
		return 0xffffffff;

	uint64_t base = (uint64_t)MAKE_HHDM(entry->address);
	uint32_t *address = (uint32_t *)(base + ((bus - entry->startbus) << 20) + (device << 15) + (function << 12));

	return *address;
}

static void mcfg_write32(int bus, int device, int function, uint32_t offset, uint32_t value) {
	mcfgentry_t *entry = getmcfgentry(bus);

	uint64_t base = (uint64_t)MAKE_HHDM(entry->address);
	uint32_t *address = (uint32_t *)(base + ((bus - entry->startbus) << 20) + (device << 15) + (function << 12));

	*address = value;
}

static void pci_archinit() {
	mcfg_t *mcfg = arch_acpi_findtable("MCFG", 0);
	if (mcfg) {
		printf("pci: MCFG table found. Using extended configuration space\n");
		mcfgentries = mcfg->entries;
		pci_archread32 = mcfg_read32;
		pci_archwrite32 = mcfg_write32;
		mcfgentrycount = (mcfg->header.length - sizeof(sdtheader_t)) / sizeof(mcfgentry_t);
		// TODO map mcfg entries as UC
	} else {
		printf("pci: MCFG table not found. Falling back to legacy access mechanism\n");
		pci_archread32 = legacy_read32;
		pci_archwrite32 = legacy_write32;
	}
}

#endif
