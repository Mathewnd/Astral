#include <arch/pci.h>
#include <kernel/vmm.h>
#include <kernel/alloc.h>
#include <math.h>
#include <kernel/pmm.h>
#include <stdio.h>
#include <arch/panic.h>
#include <assert.h>

#include <kernel/event.h>
#include <kernel/sched.h>
#include <ringbuffer.h>
#include <arch/interrupt.h>
#include <string.h>
#include <arch/idt.h>
#include <arch/cls.h>
#include <arch/isr.h>

#define CAP_QUEUESIZE 0xFFFF
#define CAP_MINPAGESIZE 0xF000000
#define CAP_DOORBELL (uint64_t)0xF00000000
#define CAP_NVMCMDSET ((uint64_t)1 << 37)

#define CC_CSS_NVM  0
#define CC_CSS_MASK 0b1110000
#define CC_MPS_MASK 0b11110000000
#define CC_AMS_MASK 0b11100000000000

#define OPCODE_IDENTIFY 0x6

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
	uint64_t asq;
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
	void* addr;
	size_t entrycount;
	int idx;
	int id;
	bool hasdata;
} queuedesc_t;

typedef struct{
	queuedesc_t sub;
	queuedesc_t comp;
	ringbuffer_t userrequests;
	event_t update;
	thread_t* worker;
} queuepair_t;

typedef struct{
	size_t maxqueue;
	size_t doorbellstride;
	volatile nvme_bar0* bar0;
	queuepair_t adminqueue;
	queuepair_t* ioqueue;
} ctlrinfo;

typedef struct{
	subqentry sub;
	compqentry comp;
	event_t completion;
} entrypair_t;

#define ASQSIZE PAGE_SIZE / sizeof(subqentry)
#define ACQSIZE PAGE_SIZE / sizeof(compqentry)

static ctlrinfo* ctlrpass;
static queuepair_t* queuepass;


static int controllercount;
static ctlrinfo** controllers;

#include <arch/io.h>

// TODO its probably really stupid to only have one irq for all controllers but eh

void nvme_irq(){
	
	controllers[0]->adminqueue.comp.hasdata = true;
	event_signal(&controllers[0]->adminqueue.update, false);
	
	apic_eoi();

}

__attribute__((noreturn)) static void nvme_workerthread(){
	
	ctlrinfo* controller = ctlrpass;
	queuepair_t* queuepair = queuepass;
	entrypair_t* userentries[256];
	int lowestfreepair = 0;
	compqentry* compq = queuepair->comp.addr;
	subqentry* subq = queuepair->sub.addr;
	volatile uint32_t* subqdoorbell = (uint32_t*)((uint8_t*)controller->bar0 + 0x1000 + queuepair->sub.id * 2 * controller->doorbellstride); 
	volatile uint32_t* compqdoorbell = (uint32_t*)((uint8_t*)controller->bar0 + 0x1000 + (queuepair->comp.id*2 + 1) * controller->doorbellstride);

	while(1){

		if(ringbuffer_datacount(&queuepair->userrequests) == 0 && queuepair->comp.hasdata == false)
			event_wait(&queuepair->update, false);

		if(ringbuffer_datacount(&queuepair->userrequests)){
			// process a user request
			
			// get the pair address and store it

			int pair = lowestfreepair;

			while(userentries[pair]) ++pair;

			__assert(ringbuffer_read(&queuepair->userrequests, &userentries[pair], sizeof(entrypair_t*)) == sizeof(entrypair_t*));
			
			userentries[pair]->sub.d0.cmdid = pair;
			
			subqentry* targentry = &subq[queuepair->sub.idx++];
			
			queuepair->sub.idx %= queuepair->sub.entrycount;
			
			*targentry = userentries[pair]->sub;

			*subqdoorbell = 1;

		}
		
		if(queuepair->comp.hasdata){
			// process requests completed by the controller
			queuepair->comp.hasdata = false;	
			
			int processed = 0;

			while(1){

				if(compq[queuepair->comp.idx].phase == 0)
					break;
				
				int pair = compq[queuepair->comp.idx].cmdid;
				
				userentries[pair]->comp = compq[queuepair->comp.idx];
				
				event_signal(&userentries[pair]->completion, false);
				
				++queuepair->comp.idx;
				queuepair->comp.idx %= queuepair->comp.entrycount;
				
				++processed;

			}	

			if(processed)
				*compqdoorbell = processed;
			
			
		}


		
	}
	
	
}

static inline void dispatchandwait(entrypair_t* e, queuepair_t* q){
	arch_interrupt_disable();
	__assert(ringbuffer_write(&q->userrequests, &e, sizeof(entrypair_t*)));
	event_signal(&q->update, false);
	event_wait(&e->completion, false);
	arch_interrupt_enable();
}

static void nvme_initctlr(pci_enumeration* pci){
	
	void* bar0p = getbarmemaddr(pci->header, 0);
	
	volatile nvme_bar0* bar0 = vmm_alloc(10, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE);

	if(!bar0)
		_panic("Out of memory", NULL);

	if(!vmm_map(bar0p, bar0, 10, ARCH_MMU_MAP_READ | ARCH_MMU_MAP_WRITE))
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

	// set relevant command stuff in the pci config space
	
	pci_setcommand(pci, PCI_COMMAND_MEMORY, 1);
	pci_setcommand(pci, PCI_COMMAND_MASTER, 1);
	
	bool msix = false;

	if(pci_msixsupport(pci)){
		msix = true;
		pci_msixenable(pci);
	}
	else{
		__assert(pci_msisupport(pci));
		pci_msienable(pci);
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

	bar0->aqa = ((ACQSIZE-1) << 16) | (ASQSIZE-1);
	
	void* adminqueues = pmm_alloc(2);

	if(!adminqueues)
		_panic("Out of memory", NULL);
	
	bar0->asq = adminqueues;
	bar0->acq = adminqueues + PAGE_SIZE;

	// enable

	bar0->cc = bar0->cc | 1;

	while((bar0->csts & 3) == 0);

	if(bar0->csts & 2){
		printf("NVMe: failed to initialize controller\n");
		return;
	}
	
	ctlrinfo* info = alloc(sizeof(ctlrinfo));

	controllers = realloc(controllers, sizeof(ctlrinfo*)*(controllercount+1));

	if(info == NULL || controllers == NULL)
		_panic("Out of memory", NULL);

	controllers[controllercount++] = info;


	info->maxqueue = (bar0->cap & CAP_QUEUESIZE) + 1;
	info->doorbellstride = 1 << (2 + ((bar0->cap & CAP_DOORBELL) >> 32));
	info->bar0 = bar0;
	
	info->adminqueue.sub.addr = MAKEHHDM(adminqueues);
	info->adminqueue.comp.addr = MAKEHHDM(adminqueues + PAGE_SIZE);
	info->adminqueue.comp.entrycount = PAGE_SIZE / sizeof(compqentry);
	info->adminqueue.sub.entrycount = PAGE_SIZE / sizeof(subqentry);
	
	if(ringbuffer_init(&info->adminqueue.userrequests, sizeof(subqentry)*64))
		_panic("Out of memory", NULL);

	ctlrpass = info;
	queuepass = &info->adminqueue;

	info->adminqueue.worker = sched_newkthread(nvme_workerthread, PAGE_SIZE*1000, true, THREAD_PRIORITY_KERNEL);
	if(!info->adminqueue.worker)
		_panic("Out of memory!", NULL);
	
	entrypair_t pair;

	memset(&pair, 0, sizeof(entrypair_t));

	pair.sub.dataptr[0] = pmm_alloc(1);
	if(!pair.sub.dataptr[0])
		_panic("Out of memory!\n", NULL);

	pair.sub.command[0] = 1;
	pair.sub.d0.opcode = OPCODE_IDENTIFY;

	if(msix)
		pci_msixadd(pci, 0, arch_getcls()->lapicid, VECTOR_NVME, true, false);
	else
		pci_msiadd(pci, arch_getcls()->lapicid, VECTOR_NVME, true, false);


	dispatchandwait(&pair, &info->adminqueue);


}

void nvme_init(){

	pci_enumeration* e = pci_getdevicecs(1, 8, 0);
		
	controllers = alloc(1);

	__assert(controllers);

	if(!e)
		return;
	
	nvme_initctlr(e);

}
