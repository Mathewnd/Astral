#include <kernel/virtio.h>
#include <kernel/pci.h>
#include <logging.h>
#include <kernel/alloc.h>
#include <kernel/pmm.h>

#define VIRTIO_VENDOR 0x1af4
#define VIRTIO_DEVICE_MIN 0x1000
#define VIRTIO_DEVICE_MAX 0x103f

// TODO fatal

static char *devname[] = {
	"network",
	"block"
};

void vioblk_init();
int vioblk_newdevice(viodevice_t *);
void vionet_init();
int vionet_newdevice(viodevice_t *);

static int (*drivedevice[]) (viodevice_t *) = {
	vionet_newdevice,
	vioblk_newdevice
};

size_t virtio_queuesize(viodevice_t *viodevice, int queue) {
	viodevice->config->queueselect = queue;
	return viodevice->config->queuesize;
}

void virtio_enablequeue(viodevice_t *viodevice, int queue) {
	viodevice->config->queueselect = queue;
	viodevice->config->queueenable = 1;
}

uint64_t virtio_negotiatefeatures(viodevice_t *viodevice, uint64_t features) {
	viodevice->config->devicefeatureselect = 0;
	uint64_t offered = viodevice->config->devicefeature;
	viodevice->config->devicefeatureselect = 1;
	offered |= (uint64_t)viodevice->config->devicefeature << 32;
	uint64_t negotiable = features & offered;

	viodevice->config->driverfeatureselect = 0;
	viodevice->config->driverfeature = negotiable & 0xffffffff;
	viodevice->config->driverfeatureselect = 1;
	viodevice->config->driverfeature = (negotiable >> 32) & 0xffffffff;

	VIO_CONFIG_STATUS_SET(viodevice, VIO_CONFIG_STATUS_FEATURESOK);
	__assert(viodevice->config->status & VIO_CONFIG_STATUS_FEATURESOK);

	return negotiable;
}

static uint16_t *virtio_queuenotifyaddress(viodevice_t *viodevice, int queue) {
       viodevice->config->queueselect = queue;
       return (uint16_t *)((uintptr_t)viodevice->notify + viodevice->config->queuenotifyoffset * viodevice->notifymultiplier);
}

void *virtio_createqueue(viodevice_t *viodevice, vioqueue_t *vioqueue, int queue, size_t size, int msix) {
	size_t bytesize = VIO_QUEUE_BYTESIZE(size);
	size_t pagesize = ROUND_UP(bytesize, PAGE_SIZE) / PAGE_SIZE;

	void *queuephys = pmm_alloc(pagesize, PMM_SECTION_DEFAULT);
	__assert(queuephys);
	memset(MAKE_HHDM(queuephys), 0, bytesize);

	vioqueue->address = queuephys;
	vioqueue->size = size;
	vioqueue->notify = virtio_queuenotifyaddress(viodevice, queue);

	viodevice->config->queueselect = queue;
	viodevice->config->queuesize = size;
	viodevice->config->queuemsixvector = msix;
	__assert(viodevice->config->queuemsixvector == msix);
	viodevice->config->queuedesc = (uint64_t)VIO_QUEUE_BUFFERS(vioqueue);
	viodevice->config->queuedriver = (uint64_t)VIO_QUEUE_DRV(vioqueue);
	viodevice->config->queuedevice = (uint64_t)VIO_QUEUE_DEV(vioqueue);

	vioqueue->address = MAKE_HHDM(queuephys);

	return queuephys;
}

void virtio_enabledevice(viodevice_t *viodevice) {
	VIO_CONFIG_STATUS_SET(viodevice, VIO_CONFIG_STATUS_DRIVEROK);
}

#define CAP_TYPE 3
#define CAP_BAR 4
#define CAP_BAROFFSET 8
#define CAP_NOTIFICATION_MULTIPLIER 16

#define CAP_TYPE_COMMONCONFIG 1
#define CAP_TYPE_NOTIFICATION 2
#define CAP_TYPE_DEVICECONFIG 4

static void virtio_newdevice(pcienum_t *e) {
	pci_setcommand(e, PCI_COMMAND_MMIO, 1);
	pci_setcommand(e, PCI_COMMAND_IO, 0);
	pci_setcommand(e, PCI_COMMAND_BUSMASTER, 1);
	pci_setcommand(e, PCI_COMMAND_IRQDISABLE, 1);

	int devtype = e->deviceid - VIRTIO_DEVICE_MIN;

	viodevice_t *viodevice = alloc(sizeof(viodevice_t));
	__assert(viodevice);

	viodevice->e = e;

	// get bars of important structures
	int i = 0;
	for (;;) {
		int off = pci_getcapoffset(e, PCI_CAP_VENDORSPECIFIC, i++);
		if (off == 0)
			break;

		uint8_t type = PCI_READ8(e, off + CAP_TYPE);
		uint8_t barnum = PCI_READ8(e, off + CAP_BAR);
		uint32_t baroffset = PCI_READ32(e, off + CAP_BAROFFSET);
		pcibar_t bar = pci_getbar(e, barnum);

		switch (type) {
			case CAP_TYPE_COMMONCONFIG:
				__assert(bar.mmio);
				viodevice->config = (volatile viocommonconfig_t *)(bar.address + baroffset);
				break;
			case CAP_TYPE_NOTIFICATION:
				__assert(bar.mmio);
				viodevice->notify = (volatile uint16_t *)(bar.address + baroffset);
				viodevice->notifymultiplier = PCI_READ32(e, off + CAP_NOTIFICATION_MULTIPLIER);
				break;
			case CAP_TYPE_DEVICECONFIG:
				__assert(bar.mmio);
				viodevice->devconfig = (volatile void *)(bar.address + baroffset);
				break;
		}
	}

	__assert(viodevice->config);
	__assert(viodevice->devconfig);
	// reset device
	viodevice->config->status = 0;

	// set acknowledge bit in status
	VIO_CONFIG_STATUS_SET(viodevice, VIO_CONFIG_STATUS_ACK);

	// set driver bit if there is a driver for it, otherwise bail out
	if (&devname[devtype] >= (char **)((uintptr_t)devname + sizeof(devname)) || devname[devtype] == NULL) {
		printf("virtio: unknown device at %02d:%02d.%d\n", e->bus, e->device, e->function);
		VIO_CONFIG_STATUS_SET(viodevice, VIO_CONFIG_STATUS_FAILED);
		free(viodevice);
		return;
	}

	printf("virtio: found %s device at %02d:%02d.%d\n", devname[devtype], e->bus, e->device, e->function);
	VIO_CONFIG_STATUS_SET(viodevice, VIO_CONFIG_STATUS_DRIVER);

	// pass control to device specific driver
	if (drivedevice[devtype](viodevice)) {
		printf("virtio: failed to start device\n");
		free(viodevice);
		return;
	}
}

void virtio_init() {
	vioblk_init();
	vionet_init();

	for (int dev = VIRTIO_DEVICE_MIN; dev <= VIRTIO_DEVICE_MAX; ++dev) {
		int i = 0;
		for (;;) {
			pcienum_t *e = pci_getenum(-1, -1, -1, VIRTIO_VENDOR, dev, -1, i++);
			if (e == NULL)
				break;
			virtio_newdevice(e);
		}
	}
}
