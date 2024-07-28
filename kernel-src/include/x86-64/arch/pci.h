#ifdef _PCIARCH_H
#error this file should only be included ONCE and only in the io/pci.c file
#else
#define _PCIARCH_H

#include <stdint.h>
#include <stddef.h>
#include <arch/io.h>
#include <arch/acpi.h>
#include <kernel/pmm.h>
#include <kernel/alloc.h>
#include <arch/cpu.h>

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

	uint64_t base = (uint64_t)entry->address;
	volatile uint32_t *address = (uint32_t *)((uintptr_t)base + (((bus - entry->startbus) << 20) | (device << 15) | (function << 12) | (offset & ~0x3)));

	return *address;
}

static void mcfg_write32(int bus, int device, int function, uint32_t offset, uint32_t value) {
	mcfgentry_t *entry = getmcfgentry(bus);

	uint64_t base = (uint64_t)entry->address;
	volatile uint32_t *address = (uint32_t *)((uintptr_t)base + (((bus - entry->startbus) << 20) | (device << 15) | (function << 12) | (offset & ~0x3)));

	*address = value;
}

static uint64_t msiformatmessage(uint32_t *data, int vector, int edgetrigger, int deassert) {
	*data = (edgetrigger ? 0 : (1 << 15)) | (deassert ? 0 : (1 << 14)) | vector;
	return 0xfee00000 | (_cpu()->id << 12);
}

// in PCIe, every function has 4096 bytes of config space for it.
// there are 8 functions per device and 32 devices per bus
#define MCFG_MAPPING_SIZE(buscount) (4096l * 8l * 32l * (buscount))

static void pci_archinit() {
	mcfg_t *mcfg = arch_acpi_findtable("MCFG", 0);

	if (mcfg) {
		printf("pci: MCFG table found. Using extended configuration space\n");
		pci_archread32 = mcfg_read32;
		pci_archwrite32 = mcfg_write32;
		mcfgentrycount = (mcfg->header.length - sizeof(sdtheader_t)) / sizeof(mcfgentry_t);
		mcfgentries = alloc(sizeof(mcfgentry_t) * mcfgentrycount);
		__assert(mcfgentries);
		memcpy(mcfgentries, mcfg->entries, sizeof(mcfgentry_t) * mcfgentrycount);
		for (int i = 0; i < mcfgentrycount; ++i) {
			void *phys = (void *)mcfgentries[i].address;
			uintmax_t pageoffset = (uintptr_t)phys % PAGE_SIZE;
			size_t mappingsize = pageoffset + MCFG_MAPPING_SIZE(mcfgentries[i].endbus - mcfgentries[i].startbus + 1);
			void *virt = vmm_map(NULL, mappingsize, VMM_FLAGS_PHYSICAL,
				ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_UC, phys);
			__assert(virt);
			mcfgentries[i].address = (uintptr_t)virt + pageoffset;
		}
	} else {
		printf("pci: MCFG table not found. Falling back to legacy access mechanism\n");
		pci_archread32 = legacy_read32;
		pci_archwrite32 = legacy_write32;
	}
}

#endif
