#include <kernel/pci.h>
#include <logging.h>
#include <kernel/vmm.h>
#include <util.h>
#include <kernel/pmm.h>

#define CC_ENABLE(cc) cc = (cc) | 1
#define CC_DISABLE(cc) cc = (cc) & ~1
#define CC_SETCOMMANDSET(cc, cmd) cc = ((cc) & ~0b1110000) | ((cmd) << 4)
#define CC_SETPAGESIZE(cc, pagesize) cc = ((cc) & ~0b11110000000) | ((pagesize) << 7)
#define CC_SETARBITRATION(cc, arbitration) cc = ((cc) & ~0b11100000000000) | ((arbitration) << 11)
#define CC_SETCOMPENTRYSIZE(cc, size) cc = ((cc) & ~0b11110000000000000000) | ((size) << 16)
#define CC_SETSUBENTRYSIZE(cc, size) cc = ((cc) & ~0b111100000000000000000000) | ((size) << 20)
#define CC_COMMANDSET_NVM 0
#define CC_ARBITRATION_ROUNDROBIN 0

#define CAP_COMMANDSET(cap) (((cap) >> 37) & 0xff)
#define CAP_MAXPAGESIZE(cap) (((cap) >> 52) & 0xf)
#define CAP_MINPAGESIZE(cap) (((cap) >> 48) & 0xf)

#define VERSION_MAJOR(v) (((v) >> 16) & 0xffff)
#define VERSION_MINOR(v) (((v) >>  8) & 0xff)
#define VERSION_PATCH(v) ((v) & 0xff)

#define STATUS_READY(s) ((s) & 1)
#define STATUS_FATAL(s) ((s) & 2)
#define STATUS_TEST(s) ((s) & 3)

#define AQATTR_SETSUBSIZE(attr, size) attr = ((attr) & ~0xfff) | (size)
#define AQATTR_SETCOMPSIZE(attr, size) attr = ((attr) & ~0xfff0000) | ((size) << 16)

typedef struct {
	uint32_t dword0;
	uint32_t namespace;
	uint64_t reserved2;
	uint64_t metadata;
	uint64_t datapointer[2];
	uint32_t command[6];
} __attribute__((packed)) subentry_t;

typedef struct {
	uint32_t value;
	uint32_t reserved;
	uint32_t subinfo;
	uint32_t commandinfo;
} __attribute__((packed)) compentry_t;

	uint32_t reserved;
	uint32_t csts;
	uint32_t nssr;
	uint32_t aqa;
	uint64_t asq;
	uint64_t acq;
typedef struct {
	uint64_t cap;
	uint32_t version;
	uint32_t irqvectormaskset;
	uint32_t irqvectormaskclear;
	uint32_t cc;
	uint32_t reserved;
	uint32_t status;
	uint32_t nvmreset;
	uint32_t aqattr;
	uint64_t asqbase;
	uint64_t acqbase;
} __attribute__((packed)) nvmebar0_t;

#define CAP_COMMANDSET_NVM 1

static void initcontroller(pcienum_t *e) {
	pcibar_t bar0p = pci_getbar(e, 0);
	volatile nvmebar0_t *bar0 = pci_mapbar(bar0p);
	__assert(bar0);

	int vmajor = VERSION_MAJOR(bar0->version);
	int vminor = VERSION_MINOR(bar0->version);
	int vpatch = VERSION_PATCH(bar0->version);

	printf("nvme: found v%d.%d.%d controller at %02x:%02x.%x\n", vmajor, vminor, vpatch, e->bus, e->device, e->function);

	if (vmajor == 1 && vminor < 3) {
		printf("nvme: controller version unsupported\n");
		return;
	}

	if ((CAP_COMMANDSET(bar0->cap) & CAP_COMMANDSET_NVM) == 0) {
		printf("nvme: controller doesn't support the NVM command set\n");
		return;
	}

	if (	(1 << (CAP_MINPAGESIZE(bar0->cap) + 12)) > PAGE_SIZE ||
		(1 << (CAP_MAXPAGESIZE(bar0->cap) + 12)) < PAGE_SIZE) {
		printf("nvme: controller doesn't support the processor page size\n");
	}

	// set command stuff in config space

	pci_setcommand(e, PCI_COMMAND_MMIO, 1);
	pci_setcommand(e, PCI_COMMAND_IO, 0);

	// enable interrupts

	if (e->msix.exists) {
		
	} else if (e->msi.exists) {
		
	} else {
		printf("nvme: controller doesn't support msi-x or msi\n");
		return;
	} 

	// reset controller
	uint32_t cc = bar0->cc;
	CC_DISABLE(cc);
	bar0->cc = cc;

	// wait for controller to reset
	while (STATUS_READY(bar0->status)) ;

	// minimal config

	CC_SETCOMMANDSET(cc, CC_COMMANDSET_NVM);
	CC_SETARBITRATION(cc, CC_ARBITRATION_ROUNDROBIN);
	CC_SETPAGESIZE(cc, ilog2(PAGE_SIZE) - 12);

	bar0->cc = cc;

	// set up admin queues
	uint32_t aqattr = 0;
	AQATTR_SETCOMPSIZE(aqattr, PAGE_SIZE / sizeof(compentry_t) - 1);
	AQATTR_SETSUBSIZE(aqattr, PAGE_SIZE / sizeof(subentry_t) - 1);

	bar0->aqattr = aqattr;

	void *acs = pmm_alloc(2, PMM_SECTION_DEFAULT);
	__assert(acs);
	memset(MAKE_HHDM(acs), 0, PAGE_SIZE * 2);
	void *asq = (void *)((uintptr_t)acs + PAGE_SIZE);

	bar0->acqbase = (uint64_t)acs;
	bar0->asqbase = (uint64_t)asq;

	// enable and wait for completion
	CC_ENABLE(cc);
	bar0->cc = cc;

	while (STATUS_TEST(bar0->status) == 0) ;

	if (STATUS_FATAL(bar0->status)) {
		printf("nvme: controller returned fatal while initializing\n");
		return;
	}
}

void nvme_init() {
	int i = 0;
	for (;;) {
		pcienum_t *e = pci_getenum(PCI_CLASS_STORAGE, PCI_SUBCLASS_STORAGE_NVM, -1, -1, -1, -1, i++);
		if (e == NULL)
			break;
		initcontroller(e);
	}
}
