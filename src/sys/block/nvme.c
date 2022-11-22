#include <arch/pci.h>
#include <kernel/vmm.h>

typedef struct{
	uint64_t cap;
	uint32_t vs;
	uint32_t intms;
	uint32_t intmc;
	uint32_t cc;
	uint32_t csts;
	uint32_t aqa;
	uint32_t asq;
	uint64_t acq;
} __attribute__((packed)) nvme_bar0;

int nvme_initctlr(pci_enumeration* pci){
	
	void* bar0p = getbarmemaddr(pci->header, 0);
	
	nvme_bar0* bar0 = vmm_alloc(2, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE);

	if(!bar0)
		_panic("Out of memory", NULL);

	if(!vmm_map(bar0p, bar0, 2, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE))
		_panic("Out of memory", NULL);

	printf("NVMe: controller detected: %02x:%02x.%x v%u cap %lx\n", pci->bus, pci->device, pci->function, bar0->vs, bar0->cap);
}

void nvme_init(){

	pci_enumeration* e = pci_getdevicecs(1, 8, 0);
	
	if(!e)
		return;
	
	nvme_initctlr(e);

}
