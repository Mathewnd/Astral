#include <kernel/virtio.h>
#include <logging.h>
#include <kernel/dpc.h>
#include <hashtable.h>
#include <kernel/slab.h>
#include <semaphore.h>
#include <kernel/block.h>
#include <kernel/page.h>
#include <string.h>
#include <resource_allocator.h>

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
	mutex_t buffers_mutex;
	spinlock_t queuelock;
	thread_t *queuewaiting[QUEUE_MAX_SIZE];
	resource_allocator_t allocator;
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

	while (blkdev->queue.lastusedindex != VIO_QUEUE_DEV_IDX(&blkdev->queue)) {
		int queue_idx = blkdev->queue.lastusedindex++ % blkdev->queue.size;
		int start_idx = VIO_QUEUE_DEV_RING(&blkdev->queue)[queue_idx].index;

		thread_t *thread = blkdev->queuewaiting[start_idx];
		blkdev->queuewaiting[start_idx] = NULL;
		__assert(thread);

		// Descriptors can be reused before the owner returns its allocator credit:
		// a new request has already reserved an equal number of credits, so this
		// just exchanges its reserved free slots for this completed chain.
		int idx = start_idx;
		for (;;) {
			int next = buffers[idx].next;
			int flags = buffers[idx].flags;
			buffers[idx].address = 0;
			if (!(flags & VIO_QUEUE_BUFFER_NEXT))
				break;
			idx = next;
		}

		sched_wakeup(thread, SCHED_WAKEUP_REASON_NORMAL);
	}
}

static void vioblk_irq(isr_t *isr, context_t *context) {
	vioblkdev_t *blkdev = isr->priv;
	dpc_enqueue(&blkdev->queuedpc, blkdev);
}

static void vioblk_enqueue(vioblkdev_t *blkdev, int index) {
	bool status = spinlock_acquire_irq_clear(&blkdev->queuelock);

	blkdev->queuewaiting[index] = current_thread();

	size_t driver_index = VIO_QUEUE_DRV_IDX(&blkdev->queue);
	VIO_QUEUE_DRV_RING(&blkdev->queue)[driver_index % blkdev->queue.size] = index;

	sched_prepare_sleep(false);

	++VIO_QUEUE_DRV_IDX(&blkdev->queue);
	*blkdev->queue.notify = 0;

	spinlock_release(&blkdev->queuelock);
	sched_yield();

	interrupt_set(status);
}

static void release_and_unlock_pages(void **pages, size_t count) {
	for (size_t i = 0; i < count; ++i)
		mm_unlock_and_release_page(pages[i]);
}

static inline size_t dma_max(vioblkdev_t *blkdev) {
	return blkdev->queue.size / 2;
}

static int get_pages(vioblkdev_t *blkdev, iovec_iterator_t *iovec_iterator, void **pages, size_t *block_count, bool write, size_t *ret_count, int *page_blocks) {
	int error;
	size_t blocks_done = 0;
	size_t pages_done = 0;
	while (pages_done < dma_max(blkdev)) {
		if (blocks_done == *block_count)
			break;

		void *page;
		size_t page_offset, page_remaining;
		error = iovec_iterator_next_page(iovec_iterator, &page_offset, &page_remaining, &page, !write);
		if (error)
			break;

		if (page == NULL) {
			error = EFAULT;
			break;
		}

		pages[pages_done++] = page + page_offset;

		if (page_remaining % 512) {
			error = EINVAL;
			break;
		}

		size_t blocks_in_page = page_remaining / 512;
		size_t blocks = min(blocks_in_page, *block_count - blocks_done);

		blocks_done += blocks;
		page_blocks[pages_done - 1] = blocks;

		// if we didnt use the whole space in the page, set the iterator back a bit
		size_t diff_between_available_and_used = page_remaining - blocks * 512;
		if (diff_between_available_and_used) {
			size_t iterator_offset = iovec_iterator_total_offset(iovec_iterator);
			iovec_iterator_set(iovec_iterator, iterator_offset - diff_between_available_and_used);
		}
	}

	if (error) {
		release_and_unlock_pages(pages, pages_done);
		return error;
	}

	*block_count = blocks_done;
	*ret_count = pages_done;
	return 0;
}

static int vioblk_rw(vioblkdev_t *blkdev, iovec_iterator_t *iovec_iterator, uintmax_t lba, size_t count, bool write) {
	void *phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
	if (phys == NULL)
		return ENOMEM;

	void **dma_pages = alloc(dma_max(blkdev) * sizeof(void *));
	if (dma_pages == NULL) {
		mm_release_page(phys);
		return ENOMEM;
	}

	int *page_blocks = alloc(dma_max(blkdev) * sizeof(int));
	if (page_blocks == NULL) {
		free(dma_pages);
		mm_release_page(phys);
		return ENOMEM;
	}

	requestheader_t *header_phys = phys;
	uint8_t *status_phys = (uint8_t *)(header_phys + 1);
	requestheader_t *header = MAKE_HHDM(header_phys);
	uint8_t *status = MAKE_HHDM(status_phys);

	volatile viobuffer_t *buffers = VIO_QUEUE_BUFFERS(&blkdev->queue);

	header->type = write ? HEADER_TYPE_WRITE : HEADER_TYPE_READ;

	size_t done = 0;
	int error = 0;
	while (done < count) {
		size_t page_count;
		size_t do_count = count - done;
		error = get_pages(blkdev, iovec_iterator, dma_pages, &do_count, write, &page_count, page_blocks);
		if (error)
			goto cleanup;

		// TODO: it would be wise to allocate this before locking the pages
		resource_allocate(&blkdev->allocator, page_count + 2);

		MUTEX_ACQUIRE(&blkdev->buffers_mutex);

		int first_buffer = -1;
		int last_buffer;
		size_t buffers_done = 0;
		for (size_t i = 0; i < blkdev->queue.size; ++i) {
			if (buffers[i].address)
				continue;

			++buffers_done;

			if (first_buffer == -1) {
				first_buffer = i;
				buffers[i].address = (uint64_t)header_phys;
				buffers[i].length = sizeof(requestheader_t);
				buffers[i].flags = 0;
				last_buffer = i;
				continue;
			}

			buffers[last_buffer].next = i;
			buffers[last_buffer].flags |= VIO_QUEUE_BUFFER_NEXT;
			last_buffer = i;

			if (buffers_done == page_count + 2) {
				buffers[i].address = (uint64_t)status_phys;
				buffers[i].length = 1;
				buffers[i].flags = VIO_QUEUE_BUFFER_DEVICE;
				break;
			}

			buffers[i].address = (uint64_t)dma_pages[buffers_done - 2];
			buffers[i].length = page_blocks[buffers_done - 2] * 512;
			buffers[i].flags = write ? 0 : VIO_QUEUE_BUFFER_DEVICE;
		}

		__assert(first_buffer != -1);
		__assert(last_buffer != first_buffer);

		MUTEX_RELEASE(&blkdev->buffers_mutex);

		header->sector = lba + done;

		vioblk_enqueue(blkdev, first_buffer);

		resource_free(&blkdev->allocator, page_count + 2);

		release_and_unlock_pages(dma_pages, page_count);

		if (*status) {
			error = EIO;
			goto cleanup;
		}

		done += do_count;
	}

	cleanup:
	free(page_blocks);
	free(dma_pages);
	mm_release_page(phys);
	return error;
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
	dpc_prepare(&blkdev->queuedpc, vioblk_dpc);

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
	__assert(virtio_queuesize(viodevice, 0) > 2 && "Here's a nickel, kid. Go get yourself a better hypervisor.");
	size_t size = min(QUEUE_MAX_SIZE, virtio_queuesize(viodevice, 0));
	virtio_createqueue(viodevice, &blkdev->queue, 0, size, 0);
	SPINLOCK_INIT(blkdev->queuelock);
	MUTEX_INIT(&blkdev->buffers_mutex);

	resource_allocator_init(&blkdev->allocator, size, dma_max(blkdev) + 2);

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
