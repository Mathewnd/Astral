#ifndef _VIRTIO_H
#define _VIRTIO_H

#include <stddef.h>
#include <stdint.h>

#include <kernel/pci.h>
#include <kernel/alloc.h>

typedef struct {
	uint32_t devicefeatureselect;
	uint32_t devicefeature;
	uint32_t driverfeatureselect;
	uint32_t driverfeature;
	uint16_t msixvector;
	uint16_t queuecount;
	uint8_t status;
	uint8_t gen;
	uint16_t queueselect;
	uint16_t queuesize;
	uint16_t queuemsixvector;
	uint16_t queueenable;
	uint16_t queuenotifyoffset;
	uint64_t queuedesc;
	uint64_t queuedriver;
	uint64_t queuedevice;
	uint16_t queuenotifydata;
	uint16_t queuereset;
} __attribute__((packed)) viocommonconfig_t;

typedef struct {
	pcienum_t *e;
	volatile viocommonconfig_t *config;
	volatile void *devconfig;
	volatile uint16_t *notify;
	size_t notifymultiplier;
} viodevice_t;

typedef struct {
	void *address;
	size_t size;
	uint16_t *notify;
	uint16_t lastusedindex;
} vioqueue_t;

void virtio_init();
size_t virtio_queuesize(viodevice_t *viodevice, int queue);
void virtio_enablequeue(viodevice_t *viodevice, int queue);
void *virtio_createqueue(viodevice_t *viodevice, vioqueue_t *vioqueue, int queue, size_t size, int msix);
void virtio_enabledevice(viodevice_t *viodevice);

#define VIO_CONFIG_STATUS_SET(x, v) (x)->config->status |= v
#define VIO_CONFIG_STATUS_ACK 1
#define VIO_CONFIG_STATUS_DRIVER 2
#define VIO_CONFIG_STATUS_DRIVEROK 4
#define VIO_CONFIG_STATUS_FEATURESOK 8
#define VIO_CONFIG_STATUS_NEEDSRESET 64
#define VIO_CONFIG_STATUS_FAILED 128

#define VIO_QUEUE_BUFFER_NEXT 1
#define VIO_QUEUE_BUFFER_DEVICE 2

typedef struct {
	uint64_t address;
	uint32_t length;
	uint16_t flags;
	uint16_t next;
} __attribute__((packed)) viobuffer_t;

typedef struct {
	uint32_t index;
	uint32_t length;
} __attribute__((packed)) viousedentry_t;

/*
struct {
	// 16 byte alignment
	struct {
		uint64_t address;
		uint32_t length;
		uint16_t flags;
		uint16_t next;
	} __attribute__((packed)) buffer[size];
	// 2 byte alignment
	struct {
		uint16_t flags;
		uint16_t index;
		uint16_t ring[size];
		uint16_t evidx;
	} __attribute__((packed)) driver;
	uint16_t padding[(size % 2) + 1];
	// 4 byte alignment
	struct {
		uint16_t flags;
		uint16_t index;
		struct {
			uint32_t index;
			uint32_t length;
		} __attribute__((packed)) usedring[size];
		uint16_t avail;
	} __attribute__((packed)) device;
} __attribute__((packed))
*/

#define VIO_QUEUE_BYTESIZE(s) (sizeof(viobuffer_t) * (s) + sizeof(uint16_t) * ((s) + 6) + sizeof(viousedentry_t) * (s))
#define VIO_QUEUE_BUFFERS(q) ((volatile viobuffer_t *)(q)->address)
#define VIO_QUEUE_DRV(q) ((volatile uint16_t *)((uintptr_t)(q)->address + (q)->size * sizeof(viobuffer_t)))
#define VIO_QUEUE_DRV_IDX(q) ((volatile uint16_t *)((uintptr_t)(q)->address + (q)->size * sizeof(viobuffer_t)))[1]
#define VIO_QUEUE_DRV_RING(q) ((volatile uint16_t *)((uintptr_t)(q)->address + (q)->size * sizeof(viobuffer_t) + 2 * sizeof(uint16_t)))
#define VIO_QUEUE_DEV(q) ((volatile uint16_t *)((uintptr_t)(q)->address + (q)->size * sizeof(viobuffer_t) + sizeof(uint16_t) * ((q)->size + 3 + (1 - ((q)->size % 2)))))
#define VIO_QUEUE_DEV_IDX(q)  ((volatile uint16_t *)((uintptr_t)(q)->address + (q)->size * sizeof(viobuffer_t) + sizeof(uint16_t) * ((q)->size + 3 + (1 - ((q)->size % 2)))))[1]
#define VIO_QUEUE_DEV_RING(q)  ((volatile viousedentry_t *)((uintptr_t)(q)->address + (q)->size * sizeof(viobuffer_t) + sizeof(uint16_t) * ((q)->size + 5 + (1 - ((q)->size % 2)))))

#endif
