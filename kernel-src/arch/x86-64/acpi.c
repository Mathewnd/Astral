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

void arch_acpi_init() {
	// FIXME search if no response
	__assert(rsdpreq.response);
	rsdp_t *rsdp = rsdpreq.response->address;
	uintmax_t pageoffset = (uintptr_t)rsdp % PAGE_SIZE;

	// map rsdp
	// make sure it doesn't cross a page boundary
	// TODO support this
	__assert(ROUND_DOWN((uintptr_t)rsdp, PAGE_SIZE) == ROUND_DOWN((uintptr_t)rsdp - 1 + sizeof(rsdp_t), PAGE_SIZE));

	void *virt = vmm_map(NULL, sizeof(rsdp_t), VMM_FLAGS_PHYSICAL, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_NOEXEC, FROM_HHDM(rsdp));
	__assert(virt);
	rsdp = (rsdp_t *)((uintptr_t)virt + pageoffset);

	if (rsdp->revision == 0) {
		printf("\e[93mACPI version 1.0 -- Using RSDT instead of XSDT.\e[0m\n");
		rsdt_t *rsdt = MAKE_HHDM((rsdt_t *)(uint64_t)rsdp->rsdt);
		__assert(arch_acpi_checksumok(rsdt));
		headercount = (rsdt->header.length - sizeof(sdtheader_t)) / sizeof(uint32_t);
		headers = alloc(sizeof(sdtheader_t *) * headercount);
		__assert(headers);

		for (size_t i = 0; i < headercount; ++i) {
			void *addr = MAKE_HHDM((void *)(uint64_t)rsdt->tables[i]);
			if (arch_acpi_checksumok(addr))
				headers[i] = addr;
		}
	} else {
		xsdt_t *xsdt = MAKE_HHDM((xsdt_t *)rsdp->xsdt);
		__assert(arch_acpi_checksumok(xsdt));
		headercount = (xsdt->header.length - sizeof(sdtheader_t)) / sizeof(uint64_t);
		headers = alloc(sizeof(sdtheader_t *) * headercount);
		__assert(headers);

		for (size_t i = 0; i < headercount; ++i) {
			void *addr = MAKE_HHDM((void *)xsdt->tables[i]);
			if (arch_acpi_checksumok(addr))
				headers[i] = addr;
		}
	}

	printf("ACPI: %lu tables found\n", headercount);
}
