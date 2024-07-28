#include <arch/acpi.h>
#include <limine.h>
#include <logging.h>
#include <kernel/alloc.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <string.h>

static size_t headercount;
static sdtheader_t **headers;

static volatile struct limine_rsdp_request rsdpreq = {
	.id = LIMINE_RSDP_REQUEST,
	.revision = 0
};

bool arch_acpi_checksumok(void *table) {
	sdtheader_t *header = table;
	uint8_t *ptr = table;
	uint8_t sum = 0;
	for (int i = 0; i < header->length; ++i)
		sum += *ptr++;

	return sum == 0;
}

void *arch_acpi_findtable(char *sig, int n) {
	for (size_t i = 0; i < headercount; ++i) {
		if (headers[i] == NULL)
			continue;

		if (strncmp(sig, headers[i]->sig, 4) == 0) {
			if (n-- == 0)
				return headers[i];
		}
	}
	return NULL;
}

static void *map_table(void *physical) {
	// TODO map according to header length entry
	uintmax_t pageoffset = (uintptr_t)physical % PAGE_SIZE;
	void *virt = vmm_map(NULL, PAGE_SIZE * 16, VMM_FLAGS_PHYSICAL, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_NOEXEC, physical);
	__assert(virt);
	return (void *)((uintptr_t)virt + pageoffset);
}

void arch_acpi_init() {
	// FIXME search if no response
	__assert(rsdpreq.response);
	rsdp_t *rsdp = map_table(FROM_HHDM(rsdpreq.response->address));

	if (rsdp->revision == 0) {
		printf("\e[93macpi: version 1.0 -- Using RSDT instead of XSDT.\e[0m\n");
		rsdt_t *rsdt = map_table((void *)(uint64_t)rsdp->rsdt);
		__assert(arch_acpi_checksumok(rsdt));

		headercount = (rsdt->header.length - sizeof(sdtheader_t)) / sizeof(uint32_t);
		headers = alloc(sizeof(sdtheader_t *) * headercount);
		__assert(headers);

		// TODO map according to the header length entry
		for (size_t i = 0; i < headercount; ++i) {
			// map it
			void *addr = map_table((void *)(uint64_t)rsdt->tables[i]);
			if (arch_acpi_checksumok(addr))
				headers[i] = addr;
		}
	} else {
		xsdt_t *xsdt = map_table((void *)rsdp->xsdt);
		__assert(arch_acpi_checksumok(xsdt));

		headercount = (xsdt->header.length - sizeof(sdtheader_t)) / sizeof(uint64_t);
		headers = alloc(sizeof(sdtheader_t *) * headercount);
		__assert(headers);

		for (size_t i = 0; i < headercount; ++i) {
			// map it
			void *addr = map_table((void *)xsdt->tables[i]);
			if (arch_acpi_checksumok(addr))
				headers[i] = addr;
		}
	}

	printf("ACPI: %lu tables found\n", headercount);
}
