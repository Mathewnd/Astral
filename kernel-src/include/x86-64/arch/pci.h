#ifdef _PCIARCH_H
#error this file should only be included ONCE and only in the io/pci.c file
#else
#define _PCIARCH_H

#include <stdint.h>
#include <stddef.h>
#include <arch/io.h>
#include <kernel/pmm.h>
#include <kernel/alloc.h>
#include <arch/cpu.h>

#include <uacpi/acpi.h>
#include <uacpi/tables.h>

static size_t mcfgentrycount;
static struct acpi_mcfg_allocation *mcfgentries;
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

static inline struct acpi_mcfg_allocation *getmcfgentry(int bus) {
	struct acpi_mcfg_allocation *entry;
	int i;

	for (i = 0; i < mcfgentrycount; ++i) {
		entry = &mcfgentries[i];
		if (bus >= entry->start_bus && bus <= entry->end_bus)
			break;
	}

	if (i == mcfgentrycount)
		entry = NULL;

	return entry;
}

static uint32_t mcfg_read32(int bus, int device, int function, uint32_t offset) {
	struct acpi_mcfg_allocation *entry = getmcfgentry(bus);
	if (entry == NULL)
		return 0xffffffff;

	uint64_t base = (uint64_t)entry->address;
	volatile uint32_t *address = (uint32_t *)((uintptr_t)base + (((bus - entry->start_bus) << 20) | (device << 15) | (function << 12) | (offset & ~0x3)));

	return *address;
}

static void mcfg_write32(int bus, int device, int function, uint32_t offset, uint32_t value) {
	struct acpi_mcfg_allocation *entry = getmcfgentry(bus);

	uint64_t base = (uint64_t)entry->address;
	volatile uint32_t *address = (uint32_t *)((uintptr_t)base + (((bus - entry->start_bus) << 20) | (device << 15) | (function << 12) | (offset & ~0x3)));

	*address = value;
}

static uint64_t msiformatmessage(uint32_t *data, int vector, int edgetrigger, int deassert) {
	*data = (edgetrigger ? 0 : (1 << 15)) | (deassert ? 0 : (1 << 14)) | vector;
	return 0xfee00000 | (current_cpu_id() << 12);
}

// in PCIe, every function has 4096 bytes of config space for it.
// there are 8 functions per device and 32 devices per bus
#define MCFG_MAPPING_SIZE(buscount) (4096l * 8l * 32l * (buscount))

static void pci_archinit() {
	uacpi_table tbl;
	struct acpi_mcfg *mcfg;

	uacpi_status ret = uacpi_table_find_by_signature("MCFG", &tbl);
	if (ret == UACPI_STATUS_OK) {
		printf("pci: MCFG table found. Using extended configuration space\n");
		mcfg = tbl.ptr;
		pci_archread32 = mcfg_read32;
		pci_archwrite32 = mcfg_write32;
		mcfgentrycount = (mcfg->hdr.length - sizeof(struct acpi_sdt_hdr)) / sizeof(struct acpi_mcfg_allocation);
		mcfgentries = alloc(sizeof(struct acpi_mcfg_allocation) * mcfgentrycount);
		__assert(mcfgentries);
		memcpy(mcfgentries, mcfg->entries, sizeof(struct acpi_mcfg_allocation) * mcfgentrycount);
		for (int i = 0; i < mcfgentrycount; ++i) {
			void *phys = (void *)mcfgentries[i].address;
			uintmax_t pageoffset = (uintptr_t)phys % PAGE_SIZE;
			size_t mappingsize = pageoffset + MCFG_MAPPING_SIZE(mcfgentries[i].end_bus - mcfgentries[i].start_bus + 1);
			void *virt = vmm_map(NULL, mappingsize, VMM_FLAGS_PHYSICAL,
				ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_UC, phys);
			__assert(virt);
			mcfgentries[i].address = (uintptr_t)virt + pageoffset;
		}
		uacpi_table_unref(&tbl);
	} else {
		printf("pci: MCFG table not found. Falling back to legacy access mechanism\n");
		pci_archread32 = legacy_read32;
		pci_archwrite32 = legacy_write32;
	}
}

#endif
