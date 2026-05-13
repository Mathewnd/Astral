#include <kernel/alloc.h>
#include <kernel/pmm.h>
#include <kernel/usb.h>
#include <logging.h>

extern usb_class_driver_t *usb_class_drivers;
extern usb_class_driver_t *usb_class_drivers_end;

int usb_submit_xfer(usb_device_t *dev, usb_xfer_t *xfer) {
	usb_ctrl_t *ctrl = dev->hub->ctrl;
	xfer->device = dev;
	return ctrl->ops->xfer(ctrl, dev, xfer);
}

int usb_control_xfer(usb_device_t *dev, usb_setup_t *setup, void *buffer, usb_completion_callback_t callback, void *ctx) {
	usb_xfer_t xfer = {0};
	xfer.flags = (setup->bmRequestType & USB_REQUEST_DIR_TO_HOST) ? USB_XFER_FLAG_TO_HOST : USB_XFER_FLAG_TO_DEVICE;
	xfer.type = USB_XFER_TYPE_CONTROL;
	xfer.setup = setup;
	xfer.data = buffer;
	xfer.data_length = setup->wLength;
	xfer.completion = callback;
	xfer.completion_ctx = ctx;

	return usb_submit_xfer(dev, &xfer);
}

static int usb_get_descriptor_for_recipient(usb_device_t *dev, uint8_t request_type, uint8_t recipient, uint16_t index, uint8_t desc_type, uint8_t desc_index, void *buffer, uint16_t length, usb_completion_callback_t callback, void *callback_ctx) {
	usb_setup_t setup = {0};
	setup.bmRequestType = recipient | request_type | USB_REQUEST_DIR_TO_HOST;
	setup.bRequest = USB_REQUEST_GET_DESCRIPTOR;
	setup.wValue = ((uint16_t)desc_type << 8) | desc_index;
	setup.wIndex = index;
	setup.wLength = length;

	return usb_control_xfer(dev, &setup, buffer, callback, callback_ctx ? callback_ctx : buffer);
}

int usb_get_device_descriptor(usb_device_t *dev, uint8_t desc_type, uint8_t desc_index, void *buffer, uint16_t length, usb_completion_callback_t callback, void *callback_ctx) {
	return usb_get_descriptor_for_recipient(dev, USB_REQUEST_STANDARD, USB_REQUEST_RECIP_DEVICE, 0, desc_type, desc_index, buffer, length, callback, callback_ctx);
}

int usb_get_class_device_descriptor(usb_device_t *dev, uint8_t desc_type, uint8_t desc_index, void *buffer, uint16_t length, usb_completion_callback_t callback, void *callback_ctx) {
	return usb_get_descriptor_for_recipient(dev, USB_REQUEST_CLASS, USB_REQUEST_RECIP_DEVICE, 0, desc_type, desc_index, buffer, length, callback, callback_ctx);
}

int usb_get_interface_descriptor(usb_device_t *dev, uint16_t interface_number, uint8_t desc_type, uint8_t desc_index, void *buffer, uint16_t length, usb_completion_callback_t callback, void *callback_ctx) {
	return usb_get_descriptor_for_recipient(dev, USB_REQUEST_STANDARD, USB_REQUEST_RECIP_INTERFACE, interface_number, desc_type, desc_index, buffer, length, callback, callback_ctx);
}

int usb_get_endpoint_descriptor(usb_device_t *dev, uint16_t endpoint_address, uint8_t desc_type, uint8_t desc_index, void *buffer, uint16_t length, usb_completion_callback_t callback, void *callback_ctx) {
	return usb_get_descriptor_for_recipient(dev, USB_REQUEST_STANDARD, USB_REQUEST_RECIP_ENDPOINT, endpoint_address, desc_type, desc_index, buffer, length, callback, callback_ctx);
}

int usb_set_configuration(usb_device_t *dev, uint8_t config_value, usb_completion_callback_t callback) {
	usb_setup_t setup = {0};
	setup.bmRequestType = USB_REQUEST_RECIP_DEVICE | USB_REQUEST_STANDARD | USB_REQUEST_DIR_TO_DEVICE;
	setup.bRequest = USB_REQUEST_SET_CONFIGURATION;
	setup.wValue = config_value;
	setup.wIndex = 0;
	setup.wLength = 0;

	return usb_control_xfer(dev, &setup, NULL, callback, NULL);
}
