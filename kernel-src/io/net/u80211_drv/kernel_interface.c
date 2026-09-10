#include <u80211_drv/kernel_interface.h>
#include <u80211_drv/status.h>
#include <kernel/wlan.h>
#include <errno.h>
#include <arch/mmu.h>
#include <kernel/alloc.h>
#include <kernel/firmware.h>
#include <kernel/page.h>
#include <kernel/timekeeper.h>
#include <kernel/usb.h>
#include <logging.h>
#include <semaphore.h>
#include <string.h>

void *u80211_drv_kernel_allocate(size_t size) {
	return alloc(size);
}

void u80211_drv_kernel_free(void *memory) {
	free(memory);
}

int u80211_drv_kernel_get_firmware(const char *name, u80211_drv_kernel_firmware_callback_t callback, void *context) {
	int error = firmware_load(name, (firmware_callback_t)callback, context);
	if (error == 0)
		return U80211_DRV_STATUS_SUCCESS;

	if (error == ENOMEM)
		return U80211_DRV_STATUS_OUT_OF_MEMORY;

	return U80211_DRV_STATUS_UNKNOWN_ERROR;
}

void u80211_drv_kernel_stall_us(unsigned int microseconds) {
	timekeeper_wait_us(microseconds);
}

void u80211_drv_kernel_print(int level, const char *msg) {
	switch (level) {
		case U80211_DRV_KERNEL_PRINT_LEVEL_INFO:
			printf("u80211_drv: [INFO] %s\n", msg);
			break;
		case U80211_DRV_KERNEL_PRINT_LEVEL_WARN:
			printf("u80211_drv: [WARN] %s\n", msg);
			break;
		case U80211_DRV_KERNEL_PRINT_LEVEL_ERROR:
			printf("u80211_drv: [ERROR] %s\n", msg);
			break;
		default:
			printf("u80211_drv: [UNKNOWN] %s\n", msg);
			break;
	}
}

int u80211_drv_kernel_get_device_descriptor(u80211_drv_device_handle_t opaque_device, u80211_drv_device_descriptor_t *descriptor) {
	usb_device_t *device = opaque_device;
	descriptor->vendor_id = device->desc.idVendor;
	descriptor->product_id = device->desc.idProduct;
	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_kernel_get_interface_descriptor(u80211_drv_interface_handle_t opaque_interface, u80211_drv_interface_descriptor_t *descriptor) {
	usb_interface_t *interface = opaque_interface;
	descriptor->number = interface->desc->bInterfaceNumber;
	descriptor->class_code = interface->desc->bInterfaceClass;
	descriptor->subclass = interface->desc->bInterfaceSubClass;
	descriptor->protocol = interface->desc->bInterfaceProtocol;
	descriptor->endpoint_count = interface->desc->bNumEndpoints;
	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_kernel_get_endpoints(u80211_drv_interface_handle_t opaque_interface, u80211_drv_endpoint_descriptor_t *endpoints, size_t endpoint_count) {
	usb_interface_t *interface = opaque_interface;

	for (size_t i = 0; i < endpoint_count; ++i) {
		usb_endpoint_desc_t *endpoint = interface->endpoints[i].desc;
		endpoints[i].address = endpoint->bEndpointAddress;
		endpoints[i].attributes = endpoint->bmAttributes;
		endpoints[i].maximum_packet_size = endpoint->wMaxPacketSize;
		endpoints[i].interval = endpoint->bInterval;
	}

	return U80211_DRV_STATUS_SUCCESS;
}

typedef struct {
	usb_device_t *device;
	usb_xfer_t xfer;
	iovec_t iovec;
	iovec_iterator_t iterator;
	u80211_drv_kernel_transfer_callback_t callback;
	void *context;
	bool active;
} u80211_drv_transfer_t;

static int u80211_drv_transfer_status(usb_status_t status) {
	if (status == USB_STATUS_SUCCESS || status == USB_STATUS_SHORT_PACKET)
		return U80211_DRV_STATUS_SUCCESS;

	return U80211_DRV_STATUS_UNKNOWN_ERROR;
}

static int u80211_drv_transfer_error(int error) {
	if (error == ENOMEM)
		return U80211_DRV_STATUS_OUT_OF_MEMORY;

	if (error == EINVAL)
		return U80211_DRV_STATUS_INVALID_ARGUMENT;

	return U80211_DRV_STATUS_UNKNOWN_ERROR;
}

static void u80211_drv_transfer_complete(usb_device_t *, void *context, usb_status_t status, size_t transferred_size) {
	u80211_drv_transfer_t *transfer = context;
	int driver_status = u80211_drv_transfer_status(status);
	if (driver_status != U80211_DRV_STATUS_SUCCESS)
		transferred_size = 0;

	__atomic_store_n(&transfer->active, false, __ATOMIC_RELEASE);
	transfer->callback(transfer->context, driver_status, transferred_size);
}

int u80211_drv_kernel_allocate_bulk_xfer(u80211_drv_device_handle_t opaque_device, uint8_t endpoint_address, void *buffer, size_t buffer_size, u80211_drv_kernel_transfer_callback_t callback, void *context, u80211_drv_transfer_handle_t *transfer_out) {
	usb_device_t *device = opaque_device;
	usb_endpoint_t *endpoint = usb_find_endpoint(device, endpoint_address);
	if (endpoint == NULL || (endpoint->desc->bmAttributes & USB_ENDPOINT_ATTRIB_TYPE_MASK) != USB_ENDPOINT_ATTRIB_TYPE_BULK)
		return U80211_DRV_STATUS_INVALID_ARGUMENT;

	u80211_drv_transfer_t *transfer = alloc(sizeof(u80211_drv_transfer_t));
	if (transfer == NULL)
		return U80211_DRV_STATUS_OUT_OF_MEMORY;

	transfer->device = device;
	transfer->iovec.addr = buffer;
	transfer->iovec.len = buffer_size;
	transfer->callback = callback;
	transfer->context = context;
	transfer->xfer.ep = endpoint;
	transfer->xfer.flags = (endpoint_address & USB_ENDPOINT_ADDRESS_DIR_IN ? USB_XFER_FLAG_TO_HOST : USB_XFER_FLAG_TO_DEVICE) | USB_XFER_FLAG_IOVEC;
	transfer->xfer.type = USB_XFER_TYPE_BULK;
	transfer->xfer.iov = &transfer->iterator;
	transfer->xfer.completion = u80211_drv_transfer_complete;
	transfer->xfer.completion_ctx = transfer;
	*transfer_out = transfer;
	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_kernel_submit_xfer(u80211_drv_transfer_handle_t opaque_transfer) {
	u80211_drv_transfer_t *transfer = opaque_transfer;

	bool expected = false;
	if (!__atomic_compare_exchange_n(&transfer->active, &expected, true, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return U80211_DRV_STATUS_INVALID_ARGUMENT;

	iovec_iterator_init(&transfer->iterator, &transfer->iovec, 1);
	int error = usb_submit_xfer(transfer->device, &transfer->xfer);
	if (error == 0)
		return U80211_DRV_STATUS_SUCCESS;

	__atomic_store_n(&transfer->active, false, __ATOMIC_RELEASE);
	return u80211_drv_transfer_error(error);
}

typedef struct {
	semaphore_t semaphore;
	usb_status_t status;
	size_t transferred_size;
} u80211_drv_sync_transfer_t;

static void u80211_drv_sync_transfer_complete(usb_device_t *, void *context, usb_status_t status, size_t transferred_size) {
	u80211_drv_sync_transfer_t *transfer = context;
	transfer->status = status;
	transfer->transferred_size = transferred_size;
	semaphore_signal(&transfer->semaphore);
}

static int u80211_drv_wait_for_transfer(u80211_drv_sync_transfer_t *transfer, int error, size_t *transferred_size) {
	if (error != 0)
		return u80211_drv_transfer_error(error);

	semaphore_wait(&transfer->semaphore, false);
	int status = u80211_drv_transfer_status(transfer->status);
	*transferred_size = status == U80211_DRV_STATUS_SUCCESS ? transfer->transferred_size : 0;
	return status;
}

int u80211_drv_kernel_submit_control_xfer_and_wait(u80211_drv_device_handle_t opaque_device, uint8_t flags, uint8_t request, uint16_t value, uint16_t index, void *buf, uint16_t buffer_size, size_t *transferred_size, unsigned int timeout) {
	(void)timeout;
	// TODO: implement timeouts once transfer cancellation is available
	// TODO: once scatter-gather control transfers are implemented, drop the bounce page stuff
	void *bounce_page = NULL;
	void *transfer_buffer = NULL;
	if (buffer_size != 0) {
		bounce_page = mm_alloc_page(MEMORY_SECTION_DEFAULT);
		if (bounce_page == NULL)
			return U80211_DRV_STATUS_OUT_OF_MEMORY;

		transfer_buffer = MAKE_HHDM(bounce_page);
		if ((flags & U80211_DRV_KERNEL_XFER_DIRECTION_MASK) == U80211_DRV_KERNEL_XFER_OUT)
			memcpy(transfer_buffer, buf, buffer_size);
	}

	usb_setup_t setup = {
		.bmRequestType = flags,
		.bRequest = request,
		.wValue = value,
		.wIndex = index,
		.wLength = buffer_size,
	};
	u80211_drv_sync_transfer_t transfer;
	SEMAPHORE_INIT(&transfer.semaphore, 0);

	int status = u80211_drv_wait_for_transfer(
		&transfer,
		usb_control_xfer(opaque_device, &setup, transfer_buffer, u80211_drv_sync_transfer_complete, &transfer),
		transferred_size
	);

	if (status == U80211_DRV_STATUS_SUCCESS && *transferred_size != 0 && (flags & U80211_DRV_KERNEL_XFER_DIRECTION_MASK) == U80211_DRV_KERNEL_XFER_IN)
		memcpy(buf, transfer_buffer, *transferred_size);

	if (bounce_page != NULL)
		mm_release_page(bounce_page);
	return status;
}

int u80211_drv_kernel_submit_bulk_xfer_and_wait(u80211_drv_device_handle_t opaque_device, uint8_t endpoint_address, void *buf, size_t buffer_size, size_t *transferred_size, unsigned int timeout) {
	(void)timeout;
	// TODO: Implement timeouts once USB transfer cancellation is available.
	usb_device_t *device = opaque_device;
	usb_endpoint_t *endpoint = usb_find_endpoint(device, endpoint_address);
	if (endpoint == NULL || (endpoint->desc->bmAttributes & USB_ENDPOINT_ATTRIB_TYPE_MASK) != USB_ENDPOINT_ATTRIB_TYPE_BULK)
		return U80211_DRV_STATUS_INVALID_ARGUMENT;

	iovec_t iovec = {
		.addr = buf,
		.len = buffer_size,
	};
	iovec_iterator_t iterator;
	iovec_iterator_init(&iterator, &iovec, 1);
	u80211_drv_sync_transfer_t transfer;
	SEMAPHORE_INIT(&transfer.semaphore, 0);
	usb_xfer_t xfer = {
		.ep = endpoint,
		.flags = (endpoint_address & USB_ENDPOINT_ADDRESS_DIR_IN ? USB_XFER_FLAG_TO_HOST : USB_XFER_FLAG_TO_DEVICE) | USB_XFER_FLAG_IOVEC,
		.type = USB_XFER_TYPE_BULK,
		.iov = &iterator,
		.completion = u80211_drv_sync_transfer_complete,
		.completion_ctx = &transfer,
	};

	return u80211_drv_wait_for_transfer(&transfer, usb_submit_xfer(device, &xfer), transferred_size);
}

int u80211_drv_device_ready(void *device, const u80211_drv_device_metadata_t *metadata, const u80211_drv_device_ops_t *ops, u80211_drv_network_device_handle_t *network_device) {
	return wlan_register(device, metadata, ops, network_device) ? U80211_DRV_STATUS_UNKNOWN_ERROR : U80211_DRV_STATUS_SUCCESS;
}

void u80211_drv_packet_received(u80211_drv_network_device_handle_t device, void *packet, size_t packet_size) {
	wlan_process_packet(device, packet, packet_size);
}
