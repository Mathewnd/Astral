#include <kernel/virtio.h>
#include <logging.h>
#include <kernel/dpc.h>
#include <hashtable.h>
#include <kernel/slab.h>
#include <semaphore.h>
#include <kernel/block.h>
#include <kernel/pmm.h>
#include <event.h>
#include <string.h>

#define QUEUE_MAX_SIZE 128

typedef struct {
	uint64_t capacity;
} __attribute__((packed)) blkdevconfig_t;

typedef struct {
	viodevice_t *viodevice;
	size_t capacity;
	int id;
	vioqueue_t queue;
	dpc_t queuedpc;
	spinlock_t queuelock;
	semaphore_t queuesem;
	event_t *queuewaiting[QUEUE_MAX_SIZE / 2];
} vioblkdev_t;

typedef struct {
	uint32_t type;
	uint32_t reserved;
	uint64_t sector;
} requestheader_t;

#define HEADER_TYPE_READ 0
#define HEADER_TYPE_WRITE 1

static hashtable_t inttable; // int id -> vioblkdev
static scache_t *headercache;

static void vioblk_dpc(context_t *context, dpcarg_t arg) {
	vioblkdev_t *blkdev = arg;
	volatile viobuffer_t *buffers = VIO_QUEUE_BUFFERS(&blkdev->queue);
	spinlock_acquire(&blkdev->queuelock);
	while (blkdev->queue.lastusedindex != VIO_QUEUE_DEV_IDX(&blkdev->queue)) {
		int idx = blkdev->queue.lastusedindex++ % blkdev->queue.size;
		int buffidx = VIO_QUEUE_DEV_RING(&blkdev->queue)[idx].index;
		__assert(blkdev->queuewaiting[buffidx / 2]);
		event_signal(blkdev->queuewaiting[buffidx / 2]);
		blkdev->queuewaiting[buffidx / 2] = NULL;
		buffers[buffidx].address = 0;
		buffers[buffidx + 1].address = 0;
		semaphore_signal(&blkdev->queuesem);
	}
	spinlock_release(&blkdev->queuelock);
}

static void vioblk_irq(isr_t *isr, context_t *context) {
	void *v;
	__assert(hashtable_get(&inttable, &v, &isr->id, sizeof(isr->id)) == 0);
	vioblkdev_t *blkdev = v;
	dpc_enqueue(&blkdev->queuedpc, vioblk_dpc, blkdev);
}

// driver and device are physical addresses
static void vioblk_enqueue(vioblkdev_t *blkdev, void *driver, size_t driverlen, void *device, size_t devicelen) {
	event_t event;
	EVENT_INIT(&event);

	bool intstatus = interrupt_set(false);
	semaphore_wait(&blkdev->queuesem, false);

	spinlock_acquire(&blkdev->queuelock);
	int idx = 0;
	volatile viobuffer_t *buffers = VIO_QUEUE_BUFFERS(&blkdev->queue);
	while (buffers[idx].address && idx < QUEUE_MAX_SIZE)
		++idx;

	__assert(idx < QUEUE_MAX_SIZE);

	buffers[idx].address = (uint64_t)driver;
	buffers[idx].length = driverlen;
	buffers[idx].flags = VIO_QUEUE_BUFFER_NEXT;
	buffers[idx].next = idx + 1;

	buffers[idx + 1].address = (uint64_t)device;
	buffers[idx + 1].length = devicelen;
	buffers[idx + 1].flags = VIO_QUEUE_BUFFER_DEVICE;

	size_t driveridx = VIO_QUEUE_DRV_IDX(&blkdev->queue)++;
	VIO_QUEUE_DRV_RING(&blkdev->queue)[driveridx % blkdev->queue.size] = idx;

	blkdev->queuewaiting[idx / 2] = &event;

	*blkdev->queue.notify = 0;
	spinlock_release(&blkdev->queuelock);

	event_wait(&event, false);

	interrupt_set(intstatus);
}

static int vioblk_rw(vioblkdev_t *blkdev, void *buffer, uintmax_t lba, size_t count, bool write) {
	void *physpage = pmm_allocpage(PMM_SECTION_DEFAULT);
	if (physpage == NULL)
		return ENOMEM;

	int err = 0;
	size_t drvlen = sizeof(requestheader_t) + (write ? 512 : 0);
	size_t devlen = write ? 1 : 513;
	requestheader_t header = {
		.type = write ? HEADER_TYPE_WRITE : HEADER_TYPE_READ
	};

	for (int off = 0; off < count; ++off) {
		header.sector = lba + off;
		memcpy(MAKE_HHDM(physpage), &header, sizeof(requestheader_t));

		if (write)
			memcpy(MAKE_HHDM((uintptr_t)physpage + sizeof(requestheader_t)), (void *)((uintptr_t)buffer + off * 512), 512);

		vioblk_enqueue(blkdev, physpage, drvlen, (void *)((uintptr_t)physpage + drvlen), devlen);

		if (*(uint8_t *)((uintptr_t)MAKE_HHDM(physpage) + drvlen + devlen - 1)) {
			err = EIO;
			break;
		}

		if (write == false)
			memcpy((void *)((uintptr_t)buffer + off * 512), MAKE_HHDM((uintptr_t)physpage + drvlen), 512);
	}

	pmm_release(physpage);
	return err;
}

static int vioblk_write(void *private, void *buffer, uintmax_t lba, size_t count) {
	return vioblk_rw(private, buffer, lba, count, true);
}

static int vioblk_read(void *private, void *buffer, uintmax_t lba, size_t count) {
	return vioblk_rw(private, buffer, lba, count, false);
}

int vioblk_newdevice(viodevice_t *viodevice) {
	if (viodevice->e->msix.exists == false) {
		printf("vioblk: device doesn't support msi-x\n");
		return 1;
	}

	pci_initmsix(viodevice->e);

	// TODO support several queues
	// no extra features will be used yet
	VIO_CONFIG_STATUS_SET(viodevice, VIO_CONFIG_STATUS_FEATURESOK);
	if ((viodevice->config->status & VIO_CONFIG_STATUS_FEATURESOK) == 0)
		return 1;

	// initialize device object
	vioblkdev_t *blkdev = alloc(sizeof(vioblkdev_t));
	__assert(blkdev);

	static int id = 0;
	volatile blkdevconfig_t *blkconfig = viodevice->devconfig;

	blkdev->viodevice = viodevice;
	blkdev->capacity = blkconfig->capacity;
	blkdev->id = id++;

	printf("vioblk%d: capacity of %lu blocks\n", blkdev->id, blkdev->capacity);

	isr_t *isr = interrupt_allocate(vioblk_irq, ARCH_EOI, IPL_DISK);
	__assert(isr);
	pci_msixadd(viodevice->e, 0, INTERRUPT_IDTOVECTOR(isr->id), 0, 0);
	pci_msixsetmask(viodevice->e, 0);
	__assert(hashtable_set(&inttable, blkdev, &isr->id, sizeof(isr->id), true) == 0);

	// initialize queue
	size_t size = min(QUEUE_MAX_SIZE, virtio_queuesize(viodevice, 0));
	virtio_createqueue(viodevice, &blkdev->queue, 0, size, 0);
	SEMAPHORE_INIT(&blkdev->queuesem, blkdev->queue.size / 2);
	SPINLOCK_INIT(blkdev->queuelock);

	virtio_enablequeue(viodevice, 0);
	virtio_enabledevice(viodevice);

	blockdesc_t blkdesc = {
		.private = blkdev,
		.blockcapacity = blkdev->capacity,
		.blocksize = 512,
		.write = vioblk_write,
		.read = vioblk_read
	};

	char name[20];
	snprintf(name, 20, "vioblk%d", blkdev->id);

	block_register(&blkdesc, name);

	return 0;
}

void vioblk_init() {
	__assert(hashtable_init(&inttable, 10) == 0);
	headercache = slab_newcache(sizeof(requestheader_t), 0, NULL, NULL);
	__assert(headercache);
}
