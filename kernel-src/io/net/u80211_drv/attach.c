#include <u80211_drv/u80211_drv.h>

#include <errno.h>
#include <kernel/alloc.h>
#include <kernel/init.h>
#include <kernel/interrupt.h>
#include <kernel/usb.h>
#include <kernel/work.h>
#include <logging.h>

typedef struct {
	usb_device_t *device;
	usb_interface_t *interface;
	work_t work;
} u80211_drv_usb_device_t;

static work_queue_t *u80211_drv_attach_queue;

static void u80211_drv_usb_attach_worker(void *context, size_t pending) {
	(void)pending;
	u80211_drv_usb_device_t *device = context;

	int status = u80211_drv_attach(device->device, device->interface);
	if (status != U80211_DRV_STATUS_SUCCESS)
		printf("u80211_drv: attachment failed with status %d\n", status);
}

static int u80211_drv_usb_probe(usb_device_t *device, usb_interface_t *interface) {
	int status = u80211_drv_probe(device, interface);
	if (status == U80211_DRV_STATUS_SUCCESS)
		return USB_DRIVER_SCORE_EXACT_MATCH;

	if (status != U80211_DRV_STATUS_NO_MATCH)
		printf("u80211_drv: probe failed with status %d\n", status);

	return USB_DRIVER_SCORE_NONE;
}

static int u80211_drv_usb_attach(usb_device_t *device, usb_interface_t *interface) {
	u80211_drv_usb_device_t *usb_device = alloc(sizeof(*usb_device));
	if (usb_device == NULL)
		return ENOMEM;

	usb_device->device = device;
	usb_device->interface = interface;
	WORK_INIT(&usb_device->work, u80211_drv_usb_attach_worker, usb_device);
	interface->driver_data = usb_device;
	work_enqueue(u80211_drv_attach_queue, &usb_device->work);
	return 0;
}

DEFINE_USB_CLASS_DRIVER(u80211_drv,
	.name = "u80211 USB NIC",
	.probe = u80211_drv_usb_probe,
	.attach = u80211_drv_usb_attach
)

static void u80211_drv_usb_init(void) {
	u80211_drv_attach_queue = work_queue_create("u80211_drv", 1, IPL_USB);
	__assert(u80211_drv_attach_queue);
}

INIT_ROUTINE_DEFINE(u80211_drv_usb, INIT_ROUTINE_FLAGS_NONE, u80211_drv_usb_init, work_queue);
