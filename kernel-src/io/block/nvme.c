#include <kernel/pci.h>
#include <logging.h>
#include <kernel/vmm.h>
#include <util.h>
#include <kernel/pmm.h>
#include <kernel/alloc.h>
#include <kernel/interrupt.h>
#include <kernel/dpc.h>
#include <hashtable.h>
#include <arch/cpu.h>
#include <errno.h>
#include <kernel/block.h>

#define CC_ENABLE(cc) cc = (cc) | 1
#define CC_DISABLE(cc) cc = (cc) & ~1
#define CC_SETCOMMANDSET(cc, cmd) cc = ((cc) & ~0b1110000) | ((cmd) << 4)
#define CC_SETPAGESIZE(cc, pagesize) cc = ((cc) & ~0b11110000000) | ((pagesize) << 7)
#define CC_SETARBITRATION(cc, arbitration) cc = ((cc) & ~0b11100000000000) | ((arbitration) << 11)
#define CC_SETSUBENTRYSIZE(cc, size) cc = ((cc) & ~0b11110000000000000000) | ((size) << 16)
#define CC_SETCOMPENTRYSIZE(cc, size) cc = ((cc) & ~0b111100000000000000000000) | ((size) << 20)
#define CC_COMMANDSET_NVM 0
#define CC_ARBITRATION_ROUNDROBIN 0

#define CAP_MAXENTRIES(cap) ((cap) & 0xff)
#define CAP_DOORBELLSTRIDE(cap) (((cap) >> 32) & 0xf)
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

#define SUB_SETDW0OPCODE(dw0, opcode) dw0 = (dw0 & ~0xff) | ((opcode) & 0xff)
#define SUB_SETDW0FUSED(dw0, fused) dw0 = (dw0 & ~0b1100000000) | (((fused) & 0x3) << 8)
#define SUB_SETDW0MEMACCESS(dw0, memaccess) dw0 = (dw0 & ~0b1100000000000000) | (((memaccess) & 0x3) << 14)
#define SUB_SETDW0ID(dw0, id) dw0 = (dw0 & ~0xffff0000) | (((id) & 0xffff) << 16)
#define SUB_INIT(sub, opcode, fused, memaccess, nsid) \
	SUB_SETDW0OPCODE((sub)->dword0, opcode); \
	SUB_SETDW0FUSED((sub)->dword0, fused); \
	SUB_SETDW0MEMACCESS((sub)->dword0, memaccess); \
	(sub)->namespace = nsid;

#define PAIR_INIT(pair, opcode, fused, memaccess, nsid) \
	SUB_INIT(&(pair)->sub, opcode, fused, memaccess, nsid);

#define SUB_DW0_OPCODE_WRITE 0x1
#define SUB_DW0_OPCODE_CREATEIOSUBQUEUE 0x1
#define SUB_DW0_OPCODE_READ 0x2
#define SUB_DW0_OPCODE_CREATEIOCOMPQUEUE 0x5
#define SUB_DW0_OPCODE_IDENTIFY 0x6
#define SUB_DW0_OPCODE_SETFEATURES 0x9
#define SUB_DW0_UNFUSED 0
#define SUB_DW0_PRP 0

#define COMP_CMDINFO_CMDID(x) ((x) & 0xffff)
#define COMP_CMDINFO_PHASE(x) (((x) >> 16) & 1)
#define COMP_CMDINFO_STATUS(x) (((x) >> 17) & 0x7fff)

#define GET_DOORBELL(bar0, id, completion, stride) (volatile uint32_t *)((uintptr_t)(bar0) + 0x1000 + (((id) * 2 + (completion)) * (4 << (stride))))

#define CTLRID_QSIZE_MIN(x) ((x) & 0xf)
#define CTLRID_QSIZE_MAX(x) (((x) >> 4) & 0xf)

typedef struct {
	uint16_t metadatasize;
	uint8_t lbadatasize; // power of two
	uint8_t relativeperformance;
} __attribute__((packed)) lbaformat_t;

typedef struct {
	uint64_t lbasize;
	uint64_t lbacapacity;
	uint64_t lbautilized;
	uint8_t features;
	uint8_t lbaformatcount;
	uint8_t lbaformattedsize;
	uint8_t metadatacap;
	uint8_t endtoendprot;
	uint8_t endtoendprotsettings;
	uint8_t sharingcap;
	uint8_t rescap;
	uint8_t fpi;
	uint8_t deallocate;
	uint16_t atomicwrite;
	uint16_t atomicwritepowerfail;
	uint16_t atomiccomparewrite;
	uint16_t atomicboundarysize;
	uint16_t atomicboundaryoffset;
	uint16_t atomicboundarypowerfail;
	uint16_t optimalioboundary;
	uint64_t nvmcapacity[2];
	uint16_t preferredwritegranularity;
	uint16_t preferredwritealignment;
	uint16_t preferreddeallocategranularity;
	uint16_t preferreddeallocatealignment;
	uint16_t optimalwritesize;
	uint8_t _reserved[18];
	uint32_t anagrpid;
	uint8_t  __reserved[3];
	uint8_t namespaceattr;
	uint16_t nvmsetid;
	uint16_t endgid;
	uint64_t namespaceguid[2];
	uint64_t eui64;
	lbaformat_t lbaformat[16];
} __attribute__((packed)) namespaceid_t;

#define CONTROLLER_TYPE_IO 1
typedef struct {
	uint16_t pcivendor;
	uint16_t pcisubsystemvendor;
	uint8_t serialnum[20];
	uint8_t modelnum[40];
	uint64_t fwrevision;
	uint8_t recommendedburst;
	uint8_t ouiid[3];
	uint8_t namespacesharing;
	uint8_t maxdatatransfer;
	uint16_t controllerid;
	uint32_t version;
	uint32_t rtd3resumelatency;
	uint32_t rtd3entrylatency;
	uint32_t optionalasyncevents;
	uint32_t attributes;
	uint16_t rrlsupported;
	uint8_t reserved[9];
	uint8_t type;
	uint64_t fguid[2];
	uint16_t crdt[3];
	uint8_t _reserved[106];
	uint8_t mi_res[16];
	uint16_t admincmdsupport;
	uint8_t abortlimit;
	uint8_t asynceventrqlimit;
	uint8_t firmwareupdates;
	uint8_t logpages;
	uint8_t errorlogpages;
	uint8_t powerstatenum;
	uint8_t adminvendorspecific;
	uint8_t apsta;
	uint16_t wctemp;
	uint16_t cctemp;
	uint16_t mtfa;
	uint32_t hmpre;
	uint32_t hmmin;
	uint64_t tnvmcap[2];
	uint64_t unvmcap[2];
	uint32_t rpmbs;
	uint16_t edstt;
	uint8_t dsto;
	uint8_t fwug;
	uint16_t kas;
	uint16_t hctma;
	uint16_t mntmt;
	uint16_t mxtmt;
	uint32_t sanicap;
	uint32_t hmminds;
	uint16_t hmmaxd;
	uint16_t nsetidmax;
	uint16_t endgidmax;
	uint8_t anatt;
	uint8_t anacap;
	uint32_t anagrpmax;
	uint32_t nanagrpid;
	uint32_t pels;
	uint8_t __reserved[156];
	uint8_t sqentrysize; // powers of 2
	uint8_t cqentrysize;
	uint16_t maxcmd;
	uint32_t namespacecount;
	uint16_t oncs;
	uint16_t fuses;
	uint8_t fna;
	uint8_t vwc;
	uint16_t awun;
	uint16_t awupf;
	uint8_t nvscc;
	uint8_t nwpc;
	uint16_t acwu;
} __attribute__((packed)) controllerid_t;

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
	uint32_t cmdinfo;
} __attribute__((packed)) compentry_t;

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

typedef struct {
	compentry_t comp;
	subentry_t sub;
	thread_t *thread;
} entrypair_t;

typedef struct {
	void *address;
	size_t entrycount;
	int index;
	volatile uint32_t *doorbell;
	int phase;
} queuedesc_t;

#define QUEUEPAIR_ENTRY_COUNT (PAGE_SIZE / sizeof(subentry_t))

typedef struct {
	queuedesc_t submission;
	queuedesc_t completion;
	struct nvmecontroller_t *controller;
	dpc_t dpc;
	spinlock_t lock;
	semaphore_t entrysem;
	entrypair_t *entries[QUEUEPAIR_ENTRY_COUNT];
} queuepair_t;

typedef struct nvmecontroller_t {
	volatile nvmebar0_t *bar0;
	int id;
	size_t dbstride;
	size_t maxentries;
	queuepair_t adminqueue;
	size_t paircount;
	uintmax_t queueindex;
	queuepair_t *ioqueues;
} nvmecontroller_t;

typedef struct {
	nvmecontroller_t *controller;
	int id;
	size_t blocksize;
	size_t capacity;
} nvmenamespace_t;

#define CAP_COMMANDSET_NVM 1

static void nvme_dpc(context_t *, dpcarg_t arg) {
	queuepair_t *pair = arg;
	spinlock_acquire(&pair->lock);

	compentry_t *queue = (compentry_t *)pair->completion.address;

	int count = 0;

	while (COMP_CMDINFO_PHASE(queue[pair->completion.index].cmdinfo) == pair->completion.phase) {
		int subid = COMP_CMDINFO_CMDID(queue[pair->completion.index].cmdinfo);
		pair->entries[subid]->comp = queue[pair->completion.index];

		sched_wakeup(pair->entries[subid]->thread, SCHED_WAKEUP_REASON_NORMAL);

		pair->entries[subid] = NULL;
		semaphore_signal(&pair->entrysem);

		++pair->completion.index;
		pair->completion.index %= pair->completion.entrycount;
		if (pair->completion.index == 0)
			pair->completion.phase = !pair->completion.phase;

		++count;
	}

	if (count)
		*pair->completion.doorbell = pair->completion.index;

	spinlock_release(&pair->lock);
}

static hashtable_t irqtable;

static void nvme_irq(isr_t *isr, context_t *context) {
	void *v;
	__assert(hashtable_get(&irqtable, &v, &isr->id, sizeof(isr->id)) == 0);
	queuepair_t *pair = v;
	dpc_enqueue(&pair->dpc, nvme_dpc, pair);
}
static isr_t *msixnewisrforqueue(queuepair_t *queuepair) {
	isr_t *isr = interrupt_allocate(nvme_irq, ARCH_EOI, IPL_DISK);
	__assert(isr);
	__assert(hashtable_set(&irqtable, queuepair, &isr->id, sizeof(isr->id), true) == 0);
	return isr;
}

static void enqueueandwait(queuepair_t *queuepair, entrypair_t *entries) {
	bool intstatus = interrupt_set(false);
	semaphore_wait(&queuepair->entrysem, false);

	subentry_t *subqueue = queuepair->submission.address;
	spinlock_acquire(&queuepair->lock);

	int pair = 0;
	while (queuepair->entries[pair]) ++pair;

	SUB_SETDW0ID(entries->sub.dword0, pair);
	queuepair->entries[pair] = entries;

	subqueue[queuepair->submission.index++] = entries->sub;
	queuepair->submission.index %= queuepair->submission.entrycount;

	*queuepair->submission.doorbell = queuepair->submission.index;
	sched_preparesleep(false);
	entries->thread = _cpu()->thread;
	spinlock_release(&queuepair->lock);
	sched_yield();
	interrupt_set(intstatus);
}

#define IDENTIFY_SIZE 4096
#define IDENTIFY_WHAT_NAMESPACE 0
#define IDENTIFY_WHAT_CONTROLLER 1
#define IDENTIFY_WHAT_NAMESPACELIST 2

static int identify(nvmecontroller_t *controller, void *buffer, int what, int namespace) {
	entrypair_t pair = {0};
	PAIR_INIT(&pair, SUB_DW0_OPCODE_IDENTIFY, SUB_DW0_UNFUSED, SUB_DW0_PRP, namespace);

	pair.sub.datapointer[0] = (uint64_t)pmm_allocpage(PMM_SECTION_DEFAULT);
	if (pair.sub.datapointer[0] == 0)
		return ENOMEM;

	pair.sub.command[0] = what;

	enqueueandwait(&controller->adminqueue, &pair);

	memcpy(buffer, MAKE_HHDM(pair.sub.datapointer[0]), IDENTIFY_SIZE);
	return COMP_CMDINFO_STATUS(pair.comp.cmdinfo) ? EIO : 0;
}

#define IDENTIFY_CONTROLLER(ctlr, buff) identify(ctlr, buff, IDENTIFY_WHAT_CONTROLLER, 0)
#define IDENTIFY_NAMESPACE(ctlr, buff, nsid) identify(ctlr, buff, IDENTIFY_WHAT_NAMESPACE, nsid)
#define IDENTIFY_NAMESPACELIST(ctlr, buff) identify(ctlr, buff, IDENTIFY_WHAT_NAMESPACELIST, 0)

#define FEATURE_NUMBEROFQUEUES 0x07
#define FEATURE_SOFTWAREPROGRESS 0x80

static int resetsoftwareprogress(nvmecontroller_t *controller) {
	entrypair_t pair = {0};
	PAIR_INIT(&pair, SUB_DW0_OPCODE_SETFEATURES, SUB_DW0_UNFUSED, SUB_DW0_PRP, 0);

	pair.sub.command[0] = FEATURE_SOFTWAREPROGRESS;

	enqueueandwait(&controller->adminqueue, &pair);

	return COMP_CMDINFO_STATUS(pair.comp.cmdinfo) ? EIO : 0;
}

static int allocateioqueues(nvmecontroller_t *controller, size_t count, size_t *returned) {
	entrypair_t pair = {0};
	PAIR_INIT(&pair, SUB_DW0_OPCODE_SETFEATURES, SUB_DW0_UNFUSED, SUB_DW0_PRP, 0);

	count -= 1;
	pair.sub.command[0] = FEATURE_NUMBEROFQUEUES;
	pair.sub.command[1] = (count << 16) | count;

	enqueueandwait(&controller->adminqueue, &pair);

	size_t subcount = pair.comp.value & 0xffff;
	size_t compcount = (pair.comp.value >> 16) & 0xffff;

	*returned = min(subcount + 1, compcount + 1);

	return COMP_CMDINFO_STATUS(pair.comp.cmdinfo) ? EIO : 0;
}

#define COMPQUEUE_INTENABLED 0x2
#define IOQUEUE_PHYSICALLYCONTIGUOUS 0x1

static int createiocompqueue(nvmecontroller_t *controller, queuedesc_t *desc, size_t entrycount, int id, int vec, int flags) {
	entrypair_t pair = {0};
	PAIR_INIT(&pair, SUB_DW0_OPCODE_CREATEIOCOMPQUEUE, SUB_DW0_UNFUSED, SUB_DW0_PRP, 0);

	size_t size = entrycount * sizeof(compentry_t);
	size_t pagesize = ROUND_UP(size, PAGE_SIZE) / PAGE_SIZE;

	void *queuepages = pmm_alloc(pagesize, PMM_SECTION_DEFAULT);
	if (queuepages == NULL)
		return ENOMEM;

	void *pageshhdm = MAKE_HHDM(queuepages);
	memset(pageshhdm, 0, size);

	pair.sub.datapointer[0] = (uint64_t)queuepages;
	pair.sub.command[0] = (id & 0xffff) | (entrycount - 1) << 16;
	pair.sub.command[1] = COMPQUEUE_INTENABLED | IOQUEUE_PHYSICALLYCONTIGUOUS | (vec << 16);

	enqueueandwait(&controller->adminqueue, &pair);

	if (COMP_CMDINFO_STATUS(pair.comp.cmdinfo)) {
		pmm_free(queuepages, pagesize);
		return EIO;
	}

	desc->address = pageshhdm;
	desc->entrycount = entrycount;
	desc->doorbell = GET_DOORBELL(controller->bar0, id, 1, controller->dbstride);
	desc->phase = 1;
	return 0;
}

static int createiosubqueue(nvmecontroller_t *controller, queuedesc_t *desc, size_t entrycount, int id, int compid, int flags) {
	entrypair_t pair = {0};
	PAIR_INIT(&pair, SUB_DW0_OPCODE_CREATEIOSUBQUEUE, SUB_DW0_UNFUSED, SUB_DW0_PRP, 0);

	size_t size = entrycount * sizeof(subentry_t);
	size_t pagesize = ROUND_UP(size, PAGE_SIZE) / PAGE_SIZE;

	void *queuepages = pmm_alloc(pagesize, PMM_SECTION_DEFAULT);
	if (queuepages == NULL)
		return ENOMEM;

	void *pageshhdm = MAKE_HHDM(queuepages);
	memset(pageshhdm, 0, size);

	pair.sub.datapointer[0] = (uint64_t)queuepages;
	pair.sub.command[0] = (id & 0xffff) | (entrycount - 1) << 16;
	pair.sub.command[1] = IOQUEUE_PHYSICALLYCONTIGUOUS | (compid << 16);

	enqueueandwait(&controller->adminqueue, &pair);

	if (COMP_CMDINFO_STATUS(pair.comp.cmdinfo)) {
		pmm_free(queuepages, pagesize);
		return EIO;
	}

	desc->address = pageshhdm;
	desc->entrycount = entrycount;
	desc->doorbell = GET_DOORBELL(controller->bar0, id, 0, controller->dbstride);
	return 0;
}

static void newioqueuepair(nvmecontroller_t *controller, queuepair_t *pair, int id) {
	__assert(createiocompqueue(controller, &pair->completion, QUEUEPAIR_ENTRY_COUNT, id, id, 0) == 0);
	__assert(createiosubqueue(controller, &pair->submission, QUEUEPAIR_ENTRY_COUNT, id, id, 0) == 0);
	pair->controller = controller;
	SPINLOCK_INIT(pair->lock);
	SEMAPHORE_INIT(&pair->entrysem, QUEUEPAIR_ENTRY_COUNT);
}

static queuepair_t *pickioqueue(nvmecontroller_t *controller) {
	uintmax_t index = __atomic_fetch_add(&controller->queueindex, 1, __ATOMIC_SEQ_CST);
	index %= controller->paircount;
	return &controller->ioqueues[index];
}

static int ioread(nvmenamespace_t *namespace, uint64_t prp[2], uint64_t lba, uint64_t count) {
	entrypair_t pair = {0};
	PAIR_INIT(&pair, SUB_DW0_OPCODE_READ, SUB_DW0_UNFUSED, SUB_DW0_PRP, namespace->id);

	queuepair_t *queue = pickioqueue(namespace->controller);

	pair.sub.datapointer[0] = prp[0];
	pair.sub.datapointer[1] = prp[1];
	pair.sub.command[0] = lba & 0xffffffff;
	pair.sub.command[1] = (lba >> 32) & 0xffffffff;
	pair.sub.command[2] = (count - 1) & 0xffff;

	enqueueandwait(queue, &pair);

	return COMP_CMDINFO_STATUS(pair.comp.cmdinfo) ? EIO : 0;
}

static int iowrite(nvmenamespace_t *namespace, uint64_t prp[2], uint64_t lba, uint64_t count) {
	entrypair_t pair = {0};
	PAIR_INIT(&pair, SUB_DW0_OPCODE_WRITE, SUB_DW0_UNFUSED, SUB_DW0_PRP, namespace->id);

	queuepair_t *queue = pickioqueue(namespace->controller);

	pair.sub.datapointer[0] = prp[0];
	pair.sub.datapointer[1] = prp[1];
	pair.sub.command[0] = lba & 0xffffffff;
	pair.sub.command[1] = (lba >> 32) & 0xffffffff;
	pair.sub.command[2] = (count - 1) & 0xffff;

	enqueueandwait(queue, &pair);

	return COMP_CMDINFO_STATUS(pair.comp.cmdinfo) ? EIO : 0;
}

#define PAGES_FLAGS (ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC)

static int rwblocks(nvmenamespace_t *namespace, void *buffer, uintmax_t lba, size_t count, bool write, bool pagealignedbuffer) {
	// TODO this is pretty inefficient and will be replaced later
	__assert(namespace->blocksize <= PAGE_SIZE);

	uint64_t prp[2] = {(uint64_t)pmm_allocpage(PMM_SECTION_DEFAULT), 0};
	int err = 0;
	if (prp[0] == 0)
		goto cleanup;

	for (int i = 0; i < count; ++i) {
		void *bufferp = (void *)((uintptr_t)buffer + i * namespace->blocksize);

		if (write) {
			memcpy(MAKE_HHDM(prp[0]), bufferp, namespace->blocksize);
		}

		err = write ? iowrite(namespace, prp, lba + i, 1) : ioread(namespace, prp, lba + i, 1);
		if (err)
			goto cleanup;

		if (!write) {
			memcpy(bufferp, MAKE_HHDM(prp[0]), namespace->blocksize);
		}
	}

	cleanup:
	if (prp[0])
		pmm_release((void *)prp[0]);

	return err;
}

static int read(void *private, void *buffer, uintmax_t lba, size_t count) {
	return rwblocks(private, buffer, lba, count, false, false);
}

static int write(void *private, void *buffer, uintmax_t lba, size_t count) {
	return rwblocks(private, buffer, lba, count, true, false);
}

static void initnamespace(nvmecontroller_t *controller, int id) {
	namespaceid_t *namespaceid = alloc(IDENTIFY_SIZE);
	__assert(namespaceid);
	__assert(IDENTIFY_NAMESPACE(controller, namespaceid, id) == 0);

	nvmenamespace_t *namespace = alloc(sizeof(nvmenamespace_t));
	__assert(namespace);

	namespace->controller = controller;
	namespace->id = id;
	namespace->capacity = namespaceid->lbacapacity;

	int lbaformat = namespaceid->lbaformattedsize & 0xf;
	namespace->blocksize = 1 << namespaceid->lbaformat[lbaformat].lbadatasize;

	printf("nvme%lun%lu: %lu blocks with %lu bytes per block\n", controller->id, id, namespace->capacity, namespace->blocksize);

	char name[10];
	snprintf(name, 10, "nvme%lun%lu", controller->id, id);

	blockdesc_t desc = {
		.private = namespace,
		.type = BLOCK_TYPE_DISK,
		.lbaoffset = 0,
		.blockcapacity = namespace->capacity,
		.blocksize = namespace->blocksize,
		.read = read,
		.write = write
	};

	block_register(&desc, name);

	free(namespaceid);
}

#define MAX_PAIRS_PER_CONTROLLER 16

static int ctlrid;
static void initcontroller(pcienum_t *e) {
	pcibar_t bar0p = pci_getbar(e, 0);
	volatile nvmebar0_t *bar0 = (volatile nvmebar0_t *)bar0p.address;
	__assert(bar0);

	pci_setcommand(e, PCI_COMMAND_MMIO, 1);
	pci_setcommand(e, PCI_COMMAND_IO, 0);
	pci_setcommand(e, PCI_COMMAND_IRQDISABLE, 1);
	pci_setcommand(e, PCI_COMMAND_BUSMASTER, 1);

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

	// enable interrupts

	size_t intcount;
	if (e->msix.exists) {
		intcount = pci_initmsix(e);
	} else if (e->msi.exists) {
		printf("nvme: driver only supports msi-x currently\n");
		return;
	} else {
		printf("nvme: controller doesn't support msi-x or msi\n");
		return;
	} 

	__assert(intcount);

	// reset controller
	uint32_t cc = bar0->cc;
	CC_DISABLE(cc);
	bar0->cc = cc;

	// wait for controller to reset
	while (STATUS_READY(bar0->status)) ;

	// minimal config

	CC_SETCOMMANDSET(cc, CC_COMMANDSET_NVM);
	CC_SETARBITRATION(cc, CC_ARBITRATION_ROUNDROBIN);
	CC_SETPAGESIZE(cc, log2(PAGE_SIZE) - 12);

	bar0->cc = cc;

	// set up admin queues
	uint32_t aqattr = 0;
	AQATTR_SETCOMPSIZE(aqattr, PAGE_SIZE / sizeof(compentry_t) - 1);
	AQATTR_SETSUBSIZE(aqattr, PAGE_SIZE / sizeof(subentry_t) - 1);

	bar0->aqattr = aqattr;

	void *acq = pmm_alloc(2, PMM_SECTION_DEFAULT);
	__assert(acq);
	memset(MAKE_HHDM(acq), 0, PAGE_SIZE * 2);
	void *asq = (void *)((uintptr_t)acq + PAGE_SIZE);

	bar0->acqbase = (uint64_t)acq;
	bar0->asqbase = (uint64_t)asq;

	// enable and wait for completion
	CC_ENABLE(cc);
	bar0->cc = cc;

	while (STATUS_TEST(bar0->status) == 0) ;

	if (STATUS_FATAL(bar0->status)) {
		printf("nvme: controller returned fatal while initializing\n");
		pmm_free(acq, 2);
		return;
	}

	nvmecontroller_t *controller = alloc(sizeof(nvmecontroller_t));
	__assert(controller);

	controller->id = ctlrid;
	controller->bar0 = bar0;
	controller->dbstride = CAP_DOORBELLSTRIDE(bar0->cap);
	controller->maxentries = CAP_MAXENTRIES(bar0->cap);

	controller->adminqueue.submission.address = MAKE_HHDM(asq);
	controller->adminqueue.submission.entrycount = PAGE_SIZE / sizeof(subentry_t);
	controller->adminqueue.submission.doorbell = GET_DOORBELL(controller->bar0, 0, 0, controller->dbstride);

	controller->adminqueue.completion.address = MAKE_HHDM(acq);
	controller->adminqueue.completion.entrycount = PAGE_SIZE / sizeof(compentry_t);
	controller->adminqueue.completion.doorbell = GET_DOORBELL(controller->bar0, 0, 1, controller->dbstride);
	controller->adminqueue.completion.phase = 1;

	SPINLOCK_INIT(controller->adminqueue.lock);
	SEMAPHORE_INIT(&controller->adminqueue.entrysem, QUEUEPAIR_ENTRY_COUNT);

	isr_t *adminisr = NULL;
	if (e->msix.exists) {
		adminisr = msixnewisrforqueue(&controller->adminqueue);
		pci_msixadd(e, 0, INTERRUPT_IDTOVECTOR(adminisr->id), 1, 0);
		pci_msixsetmask(e, 0);
	}

	// identify controller
	controllerid_t *controllerid = alloc(IDENTIFY_SIZE);
	__assert(controllerid);
	__assert(IDENTIFY_CONTROLLER(controller, controllerid) == 0);

	// <=1.3 controllers will report a 0 in this field. >=1.4 controllers have a proper type here
	if (controllerid->type != 0 && controllerid->type != CONTROLLER_TYPE_IO) {
		printf("nvme: controller type not supported\n");
		// FIXME free ISRs
		pmm_free(acq, 2);
		free(controllerid);
		free(controller);
		return;
	}

	++ctlrid;

	resetsoftwareprogress(controller);

	// validate SQ entry size and CQ entry size and set it on CC
	int minsqlog2 = CTLRID_QSIZE_MIN(controllerid->sqentrysize);
	int maxsqlog2 = CTLRID_QSIZE_MAX(controllerid->sqentrysize);
	int mincqlog2 = CTLRID_QSIZE_MIN(controllerid->cqentrysize);
	int maxcqlog2 = CTLRID_QSIZE_MAX(controllerid->cqentrysize);
	int sqlog2 = log2(sizeof(subentry_t));
	int cqlog2 = log2(sizeof(compentry_t));

	__assert(maxsqlog2 >= sqlog2 && sqlog2 >= minsqlog2);
	__assert(maxcqlog2 >= cqlog2 && cqlog2 >= mincqlog2);

	CC_SETCOMPENTRYSIZE(cc, log2(sizeof(compentry_t)));
	CC_SETSUBENTRYSIZE(cc, log2(sizeof(subentry_t)));
	bar0->cc = cc;

	// get namespace list
	uint32_t *namespacelist = alloc(IDENTIFY_SIZE);
	__assert(namespacelist);
	__assert(IDENTIFY_NAMESPACELIST(controller, namespacelist) == 0);

	// determine how many pairs to use
	size_t iomin = min(intcount, MAX_PAIRS_PER_CONTROLLER);
	size_t iocount;
	__assert(allocateioqueues(controller, iomin, &iocount) == 0);
	iocount = min(iomin, iocount);
	printf("nvme%lu: using %lu I/O queues\n", controller->id, iocount);
	controller->paircount = iocount;

	// create i/o queues
	controller->ioqueues = alloc(sizeof(queuepair_t) * iocount);
	__assert(controller->ioqueues);

	for (int i = 0; i < iocount; ++i) {
		newioqueuepair(controller, &controller->ioqueues[i], i + 1);
		if (e->msix.exists) {
			isr_t *isr = msixnewisrforqueue(&controller->ioqueues[i]);
			pci_msixadd(e, i + 1, INTERRUPT_IDTOVECTOR(isr->id), 1, 0);
		}
	}

	// initialize namespaces
	for (int i = 0; i < 1024 && namespacelist[i]; ++i)
		initnamespace(controller, namespacelist[i]);

	free(namespacelist);
	free(controllerid);
}

void nvme_init() {
	__assert(hashtable_init(&irqtable, 10) == 0);
	int i = 0;
	for (;;) {
		pcienum_t *e = pci_getenum(PCI_CLASS_STORAGE, PCI_SUBCLASS_STORAGE_NVM, -1, -1, -1, -1, i++);
		if (e == NULL)
			break;
		initcontroller(e);
	}
}
