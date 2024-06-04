#include <kernel/virtio.h>
#include <logging.h>
#include <kernel/pmm.h>
#include <kernel/eth.h>
#include <kernel/net.h>
#include <hashtable.h>
#include <arch/cpu.h>

#define MAC_FEATURE (1 << 5)
#define WANTED_FEATURES (VIO_FEATURE_VERSION_1 | MAC_FEATURE)

#define QUEUE_MAX_SIZE 256
#define BUFFER_SIZE 1526

typedef struct {
	netdev_t netdev;
	viodevice_t *viodevice;
	vioqueue_t rxqueue;
	vioqueue_t txqueue;
	thread_t *txwait[QUEUE_MAX_SIZE];
	semaphore_t txsem;
	spinlock_t txlock;
	dpc_t txdpc;
	int id;
} vionetdev_t;

typedef struct {
	uint8_t mac[6];
} __attribute__((packed)) vionetconfig_t;

typedef struct {
	uint8_t flags;
	uint8_t gsotype;
	uint16_t headerlen;
	uint16_t gsosize;
	uint16_t checksumstart;
	uint16_t checksumcount;
	uint16_t buffercount;
} __attribute__((packed)) vioframe_t;

static hashtable_t irqtable; // isr id -> netdev

static void rx_dpc(context_t *context, dpcarg_t arg) {
	vionetdev_t *netdev = arg;
	volatile viobuffer_t *buffers = VIO_QUEUE_BUFFERS(&netdev->rxqueue);
	while (netdev->rxqueue.lastusedindex != VIO_QUEUE_DEV_IDX(&netdev->rxqueue)) {
		int idx = netdev->rxqueue.lastusedindex++ % netdev->rxqueue.size;
		int buffidx = VIO_QUEUE_DEV_RING(&netdev->rxqueue)[idx].index;
		void *ethbufferphys = (void *)(buffers[buffidx].address + sizeof(vioframe_t));
		eth_process((netdev_t *)netdev, MAKE_HHDM(ethbufferphys));
		VIO_QUEUE_DRV_RING(&netdev->rxqueue)[VIO_QUEUE_DRV_IDX(&netdev->rxqueue)++ % netdev->rxqueue.size] = buffidx;
		*netdev->rxqueue.notify = 0;
	}
}

static void rx_irq(isr_t *isr, context_t *context) {
	void *v;
	__assert(hashtable_get(&irqtable, &v, &isr->id, sizeof(isr->id)) == 0);
	vionetdev_t *netdev = v;
	dpc_enqueue(&netdev->txdpc, rx_dpc, netdev);
}

static void tx_dpc(context_t *context, dpcarg_t arg) {
	vionetdev_t *netdev = arg;
	volatile viobuffer_t *buffers = VIO_QUEUE_BUFFERS(&netdev->txqueue);
	spinlock_acquire(&netdev->txlock);
	while (netdev->txqueue.lastusedindex != VIO_QUEUE_DEV_IDX(&netdev->txqueue)) {
		int idx = netdev->txqueue.lastusedindex++ % netdev->txqueue.size;
		int buffidx = VIO_QUEUE_DEV_RING(&netdev->txqueue)[idx].index;
		__assert(netdev->txwait[buffidx]);
		sched_wakeup(netdev->txwait[buffidx], SCHED_WAKEUP_REASON_NORMAL);
		netdev->txwait[buffidx] = NULL;
		buffers[buffidx].address = 0;
		semaphore_signal(&netdev->txsem);
	}
	spinlock_release(&netdev->txlock);
}

static void tx_irq(isr_t *isr, context_t *context) {
	void *v;
	__assert(hashtable_get(&irqtable, &v, &isr->id, sizeof(isr->id)) == 0);
	vionetdev_t *netdev = v;
	dpc_enqueue(&netdev->txdpc, tx_dpc, netdev);
}

#define PREFIX_SIZE (sizeof(ethframe_t) + sizeof(vioframe_t))

// requested size doesn't account for ethernet header or the virtio header
static int vionet_allocdesc(netdev_t *netdev, size_t requestedsize, netdesc_t *desc) {
	__assert(requestedsize <= netdev->mtu);
	size_t truesize = PREFIX_SIZE + requestedsize;
	void *phys = pmm_allocpage(PMM_SECTION_DEFAULT);
	if (phys == NULL)
		return ENOMEM;

	desc->address = MAKE_HHDM(phys);
	desc->size = truesize;
	desc->curroffset = PREFIX_SIZE;
	return 0;
}

static int vionet_freedesc(netdev_t *netdev, netdesc_t *desc) {
	pmm_release(FROM_HHDM(desc->address));
	return 0;
}

static int vionet_sendpacket(netdev_t *internal, netdesc_t desc, mac_t targetmac, int proto) {
	vionetdev_t *netdev = (vionetdev_t *)internal;
	vioframe_t vioframe = {
		.flags = 0,
		.gsotype = 0,
		.headerlen = PREFIX_SIZE,
		.gsosize = 0,
		.checksumstart = 0,
		.checksumcount = 0,
		.buffercount = 0
	};

	ethframe_t ethframe = {
		.type = cpu_to_be_w(proto)
	};

	memcpy(&ethframe.source, &netdev->netdev.mac, sizeof(mac_t));
	memcpy(&ethframe.destination, &targetmac, sizeof(mac_t));

	memcpy(desc.address, &vioframe, sizeof(vioframe_t));
	memcpy((void *)((uintptr_t)desc.address + sizeof(vioframe_t)), &ethframe, sizeof(ethframe_t));

	bool intstatus = interrupt_set(false);
	semaphore_wait(&netdev->txsem, false);

	spinlock_acquire(&netdev->txlock);
	int idx = 0;
	volatile viobuffer_t *buffers = VIO_QUEUE_BUFFERS(&netdev->txqueue);
	while (buffers[idx].address && idx < netdev->txqueue.size)
		++idx;

	__assert(idx < netdev->txqueue.size);

	buffers[idx].address = (uint64_t)FROM_HHDM(desc.address);
	buffers[idx].length = desc.size;
	buffers[idx].flags = 0; 

	size_t driveridx = VIO_QUEUE_DRV_IDX(&netdev->txqueue)++;
	VIO_QUEUE_DRV_RING(&netdev->txqueue)[driveridx % netdev->txqueue.size] = idx;

	sched_preparesleep(false);
	netdev->txwait[idx] = _cpu()->thread;
	*netdev->txqueue.notify = 0;

	spinlock_release(&netdev->txlock);

	sched_yield();

	interrupt_set(intstatus);
	return 0;
}

int vionet_newdevice(viodevice_t *viodevice) {
	static int id = 0;
	volatile vionetconfig_t *vionetconfig = viodevice->devconfig;

	if (viodevice->e->msix.exists == false) {
		printf("vionet: device doesn't support msi-x\n");
		return 1;
	}

	size_t intcount = pci_initmsix(viodevice->e);
	if (intcount < 2) {
		printf("vionet: not enough msi-x vectors\n");
		return 1;
	}

	uint64_t features = virtio_negotiatefeatures(viodevice, WANTED_FEATURES);
	__assert(features == WANTED_FEATURES);

	vionetdev_t *netdev = alloc(sizeof(vionetdev_t));
	__assert(netdev);
	netdev->viodevice = viodevice;
	netdev->id = id++;
	netdev->netdev.mtu = 1500;
	netdev->netdev.sendpacket = vionet_sendpacket;
	netdev->netdev.allocdesc = vionet_allocdesc;
	netdev->netdev.freedesc = vionet_freedesc;
	__assert(hashtable_init(&netdev->netdev.arpcache, 30) == 0);

	// initialize queues
	size_t rxsize = min(QUEUE_MAX_SIZE, virtio_queuesize(viodevice, 0));
	size_t txsize = min(QUEUE_MAX_SIZE, virtio_queuesize(viodevice, 1));
	virtio_createqueue(viodevice, &netdev->rxqueue, 0, rxsize, 0);
	virtio_createqueue(viodevice, &netdev->txqueue, 1, txsize, 1);

	SEMAPHORE_INIT(&netdev->txsem, txsize);
	SPINLOCK_INIT(netdev->txlock);

	isr_t *rxisr = interrupt_allocate(rx_irq, ARCH_EOI, IPL_NET);
	__assert(rxisr);
	pci_msixadd(viodevice->e, 0, INTERRUPT_IDTOVECTOR(rxisr->id), 0, 0);
	__assert(hashtable_set(&irqtable, netdev, &rxisr->id, sizeof(rxisr->id), true) == 0);

	isr_t *txisr = interrupt_allocate(tx_irq, ARCH_EOI, IPL_NET);
	__assert(txisr);
	pci_msixadd(viodevice->e, 1, INTERRUPT_IDTOVECTOR(txisr->id), 0, 0);
	__assert(hashtable_set(&irqtable, netdev, &txisr->id, sizeof(txisr->id), true) == 0);
	pci_msixsetmask(viodevice->e, 0);

	virtio_enablequeue(viodevice, 0);
	virtio_enablequeue(viodevice, 1);
	virtio_enabledevice(viodevice);

	// fill receive queues
	// XXX is the memory saving of allocating hundreds of KiBs of contiguous physical memory really worth it?
	// the system literally might not have enough (contiguous) physical memory for this, especially considering
	// this device is initialised deeper into the boot process.
	void *receivebuffer = pmm_alloc(ROUND_UP((rxsize * BUFFER_SIZE), PAGE_SIZE) / PAGE_SIZE, PMM_SECTION_DEFAULT);
	__assert(receivebuffer);

	for (int i = 0; i < rxsize; ++i) {
		VIO_QUEUE_BUFFERS(&netdev->rxqueue)[i].address = (uintptr_t)receivebuffer + i * BUFFER_SIZE;
		VIO_QUEUE_BUFFERS(&netdev->rxqueue)[i].length = BUFFER_SIZE;
		VIO_QUEUE_BUFFERS(&netdev->rxqueue)[i].flags = VIO_QUEUE_BUFFER_DEVICE;
		VIO_QUEUE_DRV_RING(&netdev->rxqueue)[i] = i;
		VIO_QUEUE_DRV_IDX(&netdev->rxqueue)++;
		*netdev->rxqueue.notify = 0;
	}

	for (int i = 0; i < 6; ++i)
		netdev->netdev.mac.address[i] = vionetconfig->mac[i];

	printf("vionet%d: mac: %02x:%02x:%02x:%02x:%02x:%02x\n", netdev->id, netdev->netdev.mac.address[0], netdev->netdev.mac.address[1], netdev->netdev.mac.address[2], netdev->netdev.mac.address[3], netdev->netdev.mac.address[4], netdev->netdev.mac.address[5]);

	char name[10];
	snprintf(name, 10, "vionet%d", netdev->id);

	__assert(netdev_register((netdev_t *)netdev, name) == 0);

	return 0;
}

void vionet_init() {
	__assert(hashtable_init(&irqtable, 20) == 0);
}
