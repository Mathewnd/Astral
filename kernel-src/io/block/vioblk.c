#include <kernel/virtio.h>
#include <logging.h>
#include <kernel/dpc.h>
#include <hashtable.h>
#include <kernel/slab.h>
#include <semaphore.h>
#include <kernel/block.h>
#include <kernel/pmm.h>
#include <string.h>

#define QUEUE_MAX_SIZE 144

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
	thread_t *queuewaiting[QUEUE_MAX_SIZE / 3];
} vioblkdev_t;

typedef struct {
	uint32_t type;
	uint32_t reserved;
	uint64_t sector;
} __attribute__((packed)) requestheader_t;

#define HEADER_TYPE_READ 0
#define HEADER_TYPE_WRITE 1

static void vioblk_dpc(context_t *context, dpcarg_t arg) {
	vioblkdev_t *blkdev = arg;
	volatile viobuffer_t *buffers = VIO_QUEUE_BUFFERS(&blkdev->queue);
	spinlock_acquire(&blkdev->queuelock);
	while (blkdev->queue.lastusedindex != VIO_QUEUE_DEV_IDX(&blkdev->queue)) {
		int idx = blkdev->queue.lastusedindex++ % blkdev->queue.size;
		int buffidx = VIO_QUEUE_DEV_RING(&blkdev->queue)[idx].index;
		__assert(blkdev->queuewaiting[buffidx / 3]);
		sched_wakeup(blkdev->queuewaiting[buffidx / 3], SCHED_WAKEUP_REASON_NORMAL);
		blkdev->queuewaiting[buffidx / 3] = NULL;
		buffers[buffidx].address = 0;
		buffers[buffidx + 1].address = 0;
		buffers[buffidx + 2].address = 0;
		semaphore_signal(&blkdev->queuesem);
	}
	spinlock_release(&blkdev->queuelock);
}

static void vioblk_irq(isr_t *isr, context_t *context) {
	vioblkdev_t *blkdev = isr->priv;
	dpc_enqueue(&blkdev->queuedpc, vioblk_dpc, blkdev);
}

// driver and device are physical addresses
static void vioblk_enqueue(vioblkdev_t *blkdev, requestheader_t *headerphys, void *bufferphys, size_t bufferlen, uint8_t *statusphys, bool write) {
	bool intstatus = interrupt_set(false);
	semaphore_wait(&blkdev->queuesem, false);

	spinlock_acquire(&blkdev->queuelock);
	int idx = 0;
	volatile viobuffer_t *buffers = VIO_QUEUE_BUFFERS(&blkdev->queue);
	while (buffers[idx].address && idx < blkdev->queue.size)
		idx += 3;

	__assert(idx < blkdev->queue.size);

	buffers[idx].address = (uint64_t)headerphys;
	buffers[idx].length = sizeof(requestheader_t);
	buffers[idx].flags = VIO_QUEUE_BUFFER_NEXT;
	buffers[idx].next = idx + 1;

	buffers[idx + 1].address = (uint64_t)bufferphys;
	buffers[idx + 1].length = bufferlen;
	buffers[idx + 1].flags = (write ? 0 : VIO_QUEUE_BUFFER_DEVICE) | VIO_QUEUE_BUFFER_NEXT;
	buffers[idx + 1].next = idx + 2;

	buffers[idx + 2].address = (uint64_t)statusphys;
	buffers[idx + 2].length = 1;
	buffers[idx + 2].flags = VIO_QUEUE_BUFFER_DEVICE;

	size_t driveridx = VIO_QUEUE_DRV_IDX(&blkdev->queue);
	VIO_QUEUE_DRV_RING(&blkdev->queue)[driveridx % blkdev->queue.size] = idx;

	sched_prepare_sleep(false);
	blkdev->queuewaiting[idx / 3] = current_thread();

	++VIO_QUEUE_DRV_IDX(&blkdev->queue);
	*blkdev->queue.notify = 0;
	spinlock_release(&blkdev->queuelock);

	sched_yield();

	interrupt_set(intstatus);
}

static int vioblk_rw(vioblkdev_t *blkdev, iovec_iterator_t *iovec_iterator, uintmax_t lba, size_t count, bool write) {
	void *physpage = pmm_allocpage(PMM_SECTION_DEFAULT);
	if (physpage == NULL)
		return ENOMEM;

	int err = 0;
	requestheader_t *header = physpage;
	requestheader_t *headerhhdm = MAKE_HHDM(physpage);
	uint8_t *status = (uint8_t *)(header + 1);
	uint8_t *statushhdm = MAKE_HHDM(status);

	headerhhdm->type = write ? HEADER_TYPE_WRITE : HEADER_TYPE_READ;

	int done = 0;
	while (done < count) {
		void *page;
		size_t page_offset, page_remaining;
		err = iovec_iterator_next_page(iovec_iterator, &page_offset, &page_remaining, &page);
		if (err)
			break;

		// check that there is enough space to complete the write of this page
		__assert(page);
		__assert(page_remaining >= min((count - done) * 512, PAGE_SIZE));

		// and that the space is aligned to the block size
		__assert((page_remaining % 512) == 0);

		headerhhdm->sector = lba + done;
		size_t docount = page_remaining / 512;
		docount = min(docount, count - done);

		vioblk_enqueue(blkdev, header, (void *)((uintptr_t)page + page_offset), PAGE_SIZE, status, write);

		pmm_release(page);

		// if we didnt use the whole space in the page, set the iterator back a bit
		size_t diff_between_available_and_used = page_remaining - docount * 512;
		if (diff_between_available_and_used) {
			size_t iterator_offset = iovec_iterator_total_offset(iovec_iterator);
			iovec_iterator_set(iovec_iterator, iterator_offset - diff_between_available_and_used);
		}

		if (*statushhdm) {
			err = EIO;
			break;
		}

		done += docount;
	}

	pmm_release(physpage);
	return err;
}

static int vioblk_write(void *private, iovec_iterator_t *iovec_iterator, uintmax_t lba, size_t count) {
	return vioblk_rw(private, iovec_iterator, lba, count, true);
}

static int vioblk_read(void *private, iovec_iterator_t *iovec_iterator, uintmax_t lba, size_t count) {
	return vioblk_rw(private, iovec_iterator, lba, count, false);
}

int vioblk_newdevice(viodevice_t *viodevice) {
	if (viodevice->e->msix.exists == false) {
		printf("vioblk: device doesn't support msi-x\n");
		return 1;
	}

	pci_initmsix(viodevice->e);

	// TODO support several queues
	__assert(virtio_negotiatefeatures(viodevice, VIO_FEATURE_VERSION_1) == VIO_FEATURE_VERSION_1);

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
	isr->priv = blkdev;
	pci_msixadd(viodevice->e, 0, INTERRUPT_IDTOVECTOR(isr->id), 0, 0);
	pci_msixsetmask(viodevice->e, 0);

	// initialize queue
	size_t size = min(QUEUE_MAX_SIZE, virtio_queuesize(viodevice, 0));
	size = size - (size % 3);
	virtio_createqueue(viodevice, &blkdev->queue, 0, size, 0);
	SEMAPHORE_INIT(&blkdev->queuesem, blkdev->queue.size / 3);
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
