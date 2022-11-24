#include <arch/pci.h>
#include <kernel/vmm.h>
#include <kernel/alloc.h>
#include <math.h>
#include <kernel/pmm.h>
#include <stdio.h>
#include <arch/panic.h>

#define CAP_QUEUESIZE 0xFFFF
#define CAP_MINPAGESIZE 0xF000000
#define CAP_DOORBELL (uint64_t)0xF00000000
#define CAP_NVMCMDSET ((uint64_t)1 << 37)

#define CC_CSS_NVM  0
#define CC_CSS_MASK 0b1110000
#define CC_MPS_MASK 0b11110000000
#define CC_AMS_MASK 0b11100000000000

typedef struct{
	uint64_t cap;
	uint32_t vs;
	uint32_t intms;
	uint32_t intmc;
	uint32_t cc;
	uint32_t reserved;
	uint32_t csts;
	uint32_t nssr;
	uint32_t aqa;
	uint32_t asq;
	uint64_t acq;
} __attribute__((packed)) nvme_bar0;

typedef struct{
	uint8_t opcode;
	uint8_t fused : 2;
	uint8_t reserved : 4;
	uint8_t prporsgl : 2;
	uint16_t cmdid;
} __attribute((packed)) subqdword0;

typedef struct{
	subqdword0 d0;
	uint32_t namespace;
	uint32_t reserved[2];
	uint64_t metadata;
	uint64_t dataptr[2];
	uint32_t command[6];
} __attribute__((packed)) subqentry;

typedef struct{
	uint32_t cmdspecific;
	uint32_t reserved;
	uint16_t subqueueptr;
	uint16_t subqueueid;
	uint16_t cmdid;
	uint16_t phase : 1;
	uint16_t status : 15;
} __attribute__((packed)) compqentry;

typedef struct{
	size_t maxqueue;
	size_t doorbellstride;
	nvme_bar0* bar0;
	subqentry* adminsubq;
	compqentry* admincompq;
} ctlrinfo;

#define ASQSIZE PAGE_SIZE / sizeof(subqentry)
#define ACQSIZE PAGE_SIZE / sizeof(compqentry)

void nvme_initctlr(pci_enumeration* pci){
	
	void* bar0p = getbarmemaddr(pci->header, 0);
	
	nvme_bar0* bar0 = vmm_alloc(2, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE);

	if(!bar0)
		_panic("Out of memory", NULL);

	if(!vmm_map(bar0p, bar0, 2, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE))
		_panic("Out of memory", NULL);

	int maj = bar0->vs >> 16;
	int min = (bar0->vs >> 8) & 0xFF;

	printf("NVMe: controller detected: %02x:%02x.%x v%u.%u cap %lx\n", pci->bus, pci->device, pci->function, maj, min, bar0->cap);
	
	if(maj == 1 && min < 4){
		printf("Unsupported nvme version!\n");
		return;
	}

	if((bar0->cap & CAP_NVMCMDSET) == 0){
		printf("Controller does not support the NVM command set!");
		return;
	}

	if(intpow(2, 12 + (bar0->cap & CAP_MINPAGESIZE) >> 32) > PAGE_SIZE){
		printf("Controller minimum page size is bigger than the system page size\n");
		return;
	}
	
	// reset controller

	uint32_t cc = bar0->cc;

	cc &= ~1;
	bar0->cc = cc;

	// set config
	
	// NVM comand set
	
	cc &= ~CC_CSS_MASK;

	// 4k Page size
	
	cc &= ~CC_MPS_MASK;

	// round robin AMS
	
	cc &= ~CC_AMS_MASK; 

	// set the config and enable

	bar0->cc = cc;

	// enable
	
	bar0->cc |= 1;

	while((bar0->csts & 3) == 0);

	if(bar0->csts & 2){
		printf("NVMe: failed to initialize controller\n");
		return;
	}
	
	ctlrinfo* info = alloc(sizeof(ctlrinfo));
	
	if(!info)
		_panic("Out of memory", NULL);

	info->maxqueue = (bar0->cap & CAP_QUEUESIZE) + 1;
	info->doorbellstride = (bar0->cap & CAP_DOORBELL) >> 32;
	info->bar0 = bar0;

	bar0->aqa = ((ACQSIZE-1) << 16) | (ASQSIZE-1);
	
	void* adminqueues = pmm_alloc(2);

	if(!adminqueues)
		_panic("Out of memory", NULL);
	
	bar0->asq = adminqueues;
	bar0->acq = adminqueues + PAGE_SIZE;
	


}

void nvme_init(){

	pci_enumeration* e = pci_getdevicecs(1, 8, 0);
	
	if(!e)
		return;
	
	nvme_initctlr(e);

}
