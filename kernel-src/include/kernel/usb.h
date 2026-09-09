#ifndef _USB_H
#define _USB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <kernel/iovec.h>
#include <list.h>

typedef struct usb_hub_port usb_hub_port_t;
typedef struct usb_hub usb_hub_t;
typedef struct usb_device usb_device_t;
typedef struct usb_interface usb_interface_t;
typedef struct usb_endpoint usb_endpoint_t;
typedef struct usb_xfer usb_xfer_t;
typedef struct usb_ctrl usb_ctrl_t;
typedef struct usb_class_driver usb_class_driver_t;

#define USB_DESCRIPTOR_TYPE_DEVICE 1
#define USB_DESCRIPTOR_TYPE_CONFIG 2
#define USB_DESCRIPTOR_TYPE_STRING 3
#define USB_DESCRIPTOR_TYPE_INTERFACE 4
#define USB_DESCRIPTOR_TYPE_ENDPOINT 5
#define USB_DESCRIPTOR_TYPE_SS_EP_COMPANION 48
#define USB_DESCRIPTOR_TYPE_HUB 0x29
#define USB_DESCRIPTOR_TYPE_SS_HUB 0x2A

typedef struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
} usb_desc_hdr_t;

#define USB_MAX_CONFIG_DESC_SIZE 4096
#define USB_CLASS_HUB 0x09

typedef struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
} __attribute__((packed)) usb_device_desc_t;

typedef struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
	uint8_t bmAttributes;
	uint8_t bMaxPower;
} __attribute__((packed)) usb_config_desc_t;

typedef struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
} __attribute__((packed)) usb_interface_desc_t;

#define USB_ENDPOINT_ADDRESS_NUM_MASK 0x0f
#define USB_ENDPOINT_ADDRESS_DIR_IN 0x80

#define USB_ENDPOINT_ATTRIB_TYPE_MASK 0x03
#define USB_ENDPOINT_ATTRIB_TYPE_CONTROL 0x00
#define USB_ENDPOINT_ATTRIB_TYPE_ISOCH 0x01
#define USB_ENDPOINT_ATTRIB_TYPE_BULK 0x02
#define USB_ENDPOINT_ATTRIB_TYPE_INTR 0x03

typedef struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
} __attribute__((packed)) usb_endpoint_desc_t;

#define USB_ENDPOINT_MAX_PACKET_SIZE_MASK 0x7ff

static inline uint16_t usb_endpoint_max_packet_size(usb_endpoint_desc_t *desc) {
	return desc->wMaxPacketSize & USB_ENDPOINT_MAX_PACKET_SIZE_MASK;
}

typedef struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bMaxBurst;
	uint8_t bmAttributes;
	uint16_t wBytesPerInterval;
} __attribute__((packed)) usb_ss_ep_companion_desc_t;

#define USB_HUB_PROTOCOL_MULTI_TT 2

typedef struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bNbrPorts;
	uint16_t wHubCharacteristics;
	uint8_t bPwrOn2PwrGood;
	uint8_t bHubContrCurrent;
} __attribute__((packed)) usb_hub_desc_t;

typedef struct {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bNbrPorts;
	uint16_t wHubCharacteristics;
	uint8_t bPwrOn2PwrGood;
	uint8_t bHubContrCurrent;
	uint8_t bHubHdrDecLat;
	uint16_t wHubDelay;
} __attribute__((packed)) usb_ss_hub_desc_t;

#define USB_REQUEST_RECIP_DEVICE 0x00
#define USB_REQUEST_RECIP_INTERFACE 0x01
#define USB_REQUEST_RECIP_ENDPOINT 0x02
#define USB_REQUEST_RECIP_OTHER 0x03

#define USB_REQUEST_STANDARD 0x00
#define USB_REQUEST_CLASS 0x20
#define USB_REQUEST_VENDOR 0x40

#define USB_REQUEST_DIR_TO_DEVICE 0x00
#define USB_REQUEST_DIR_TO_HOST 0x80

#define USB_REQUEST_GET_DESCRIPTOR 6
#define USB_REQUEST_SET_CONFIGURATION 9
#define USB_REQUEST_SET_INTERFACE 11

typedef struct {
	uint8_t bmRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
} __attribute__((packed)) usb_setup_t;

typedef enum {
	USB_HUB_PORT_DISCONNECTED,
	USB_HUB_PORT_RESETTING,
	USB_HUB_PORT_ENABLED,
} usb_hub_port_status_t;

struct usb_hub_port {
	usb_device_t *device;
	uint32_t generation;
};

#define USB_HUB_PORT_STATUS_PORT_CONNECTION (1 << 0)
#define USB_HUB_PORT_STATUS_PORT_ENABLE (1 << 1)
#define USB_HUB_PORT_STATUS_PORT_OVER_CURRENT (1 << 3)
#define USB_HUB_PORT_STATUS_PORT_RESET (1 << 4)
#define USB_HUB_PORT_STATUS_PORT_POWER (1 << 8)
#define USB_HUB_PORT_STATUS_LOW_SPEED (1 << 9)
#define USB_HUB_PORT_STATUS_HIGH_SPEED (1 << 10)

#define USB_HUB_PORT_CHANGE_PORT_CONNECTION (1 << 0)
#define USB_HUB_PORT_CHANGE_PORT_ENABLE (1 << 1)
#define USB_HUB_PORT_CHANGE_PORT_OVER_CURRENT (1 << 3)
#define USB_HUB_PORT_CHANGE_PORT_RESET (1 << 4)

#define USB_HUB_FEATURE_PORT_CONNECTION 0
#define USB_HUB_FEATURE_PORT_ENABLE 1
#define USB_HUB_FEATURE_PORT_RESET 4
#define USB_HUB_FEATURE_PORT_POWER 8
#define USB_HUB_FEATURE_PORT_LOW_SPEED 9
#define USB_HUB_FEATURE_C_PORT_CONNECTION 16
#define USB_HUB_FEATURE_C_PORT_ENABLE 17
#define USB_HUB_FEATURE_C_PORT_OVER_CURRENT 19
#define USB_HUB_FEATURE_C_PORT_RESET 20

typedef struct {
	int (*get_port_status)(usb_hub_t *, uint8_t port, uint16_t *status, uint16_t *change);
	int (*set_port_feature)(usb_hub_t *, uint8_t port, uint16_t feature);
	int (*clear_port_feature)(usb_hub_t *, uint8_t port, uint16_t feature);
} usb_hub_ops_t;

struct usb_hub {
	char name[32];

	// List node for linking hubs in the controller's hub list.
	list_node_t node;
	// Controller the hub is connected to.
	usb_ctrl_t *ctrl;
	// Operations for managing the hub.
	usb_hub_ops_t *ops;
	// Pointer to the device representing the hub itself.
	// NULL if the hub is a root hub.
	usb_device_t *device;
	// Array of ports on the hub.
	// Each port can have a pointer to the connected device.
	usb_hub_port_t *ports;
	uint32_t port_count;
};

struct usb_endpoint {
	usb_endpoint_desc_t *desc;
	usb_ss_ep_companion_desc_t *ss_companion;
};

struct usb_interface {
	usb_interface_desc_t *desc;
	usb_endpoint_t *endpoints;
	// Pointer to the attached class driver (for cleanup on disconnect).
	usb_class_driver_t *driver;
	// Private data for the attached class driver.
	void *driver_data;
};

typedef enum {
	USB_SPEED_UNKNOWN,
	USB_SPEED_LOW,
	USB_SPEED_FULL,
	USB_SPEED_HIGH,
	USB_SPEED_SUPER,
	USB_SPEED_SUPER_PLUS,
} usb_speed_t;

static inline bool usb_device_desc_valid(usb_device_desc_t *desc, usb_speed_t speed, size_t transferred) {
	if (transferred < sizeof(usb_device_desc_t))
		return false;

	if (desc->bLength != sizeof(usb_device_desc_t))
		return false;

	if (desc->bDescriptorType != USB_DESCRIPTOR_TYPE_DEVICE)
		return false;

	if (desc->bNumConfigurations == 0)
		return false;

	if (speed == USB_SPEED_SUPER || speed == USB_SPEED_SUPER_PLUS)
		return desc->bMaxPacketSize0 == 9;

	if (speed == USB_SPEED_LOW)
		return desc->bMaxPacketSize0 == 8;

	if (speed == USB_SPEED_HIGH)
		return desc->bMaxPacketSize0 == 64;

	if (speed == USB_SPEED_FULL) {
		return desc->bMaxPacketSize0 == 8 ||
			desc->bMaxPacketSize0 == 16 ||
			desc->bMaxPacketSize0 == 32 ||
			desc->bMaxPacketSize0 == 64;
	}

	return false;
}

struct usb_device {
	// Pointer to the parent hub.
	usb_hub_t *hub;
	// Descriptors for device
	usb_device_desc_t desc;
	usb_config_desc_t *config_desc;
	usb_desc_hdr_t *hub_desc;
	// Port number on the parent hub.
	uint8_t port_number;
	// Hub-port generation captured when enumeration started.
	uint32_t port_generation;
	// Read only USB address assigned to the device.
	uint8_t address;
	// Device speed.
	usb_speed_t speed;
	// Interfaces
	usb_interface_t *interfaces;
};

static inline usb_endpoint_t *usb_find_endpoint(usb_device_t *device, uint8_t endpoint_address) {
	for (uint8_t i = 0; i < device->config_desc->bNumInterfaces; ++i) {
		usb_interface_t *interface = &device->interfaces[i];
		for (uint8_t j = 0; j < interface->desc->bNumEndpoints; ++j) {
			usb_endpoint_t *endpoint = &interface->endpoints[j];
			if (endpoint->desc->bEndpointAddress == endpoint_address)
				return endpoint;
		}
	}

	return NULL;
}

typedef enum {
	USB_XFER_FLAG_TO_DEVICE = (1 << 0),
	USB_XFER_FLAG_TO_HOST = (1 << 1),
	USB_XFER_FLAG_IOVEC = (1 << 2),
	USB_XFER_FLAG_BUFFER_PHYSICAL = (1 << 3),
} usb_xfer_flag_t;

typedef enum {
	USB_XFER_TYPE_CONTROL,
	USB_XFER_TYPE_BULK,
	USB_XFER_TYPE_INTERRUPT,
} usb_xfer_type_t;

typedef enum {
	USB_STATUS_SUCCESS,
	USB_STATUS_SHORT_PACKET,
	USB_STATUS_ERROR,
	USB_STATUS_STALL,
	USB_STATUS_FLOW_ERROR,
} usb_status_t;

typedef void (*usb_completion_callback_t)(usb_device_t *device, void *completion_ctx, usb_status_t status, size_t transferred);

// Represents a USB control or data transfer.
struct usb_xfer {
	// Target endpoint for the transfer.
	// Should be NULL and will be ignored for control transfers.
	usb_endpoint_t *ep;

	// Flags for the transfer.
	usb_xfer_flag_t flags;
	usb_xfer_type_t type;

	// Only valid for control transfers.
	usb_setup_t *setup;

	// Buffer for the transfer.
	union {
		iovec_iterator_t *iov;
		struct {
			void *data;
			size_t data_length;
		};
	};

	// Asynchronous completion callback.
	usb_completion_callback_t completion;
	usb_device_t *device;
	void *completion_ctx;
};

typedef void (*usb_address_device_callback_t)(usb_hub_t *hub, uint8_t port, usb_device_t *device);

typedef struct {
	// Allocate, enable and address a USB device.
	int (*address_device)(usb_ctrl_t *, usb_hub_t *, uint8_t port, usb_speed_t speed, usb_address_device_callback_t callback);
	// Deaddress, disable and free a USB device
	void (*deaddress_device)(usb_ctrl_t *, usb_device_t *);
	// Mark a device as a hub in the controller
	int (*mark_as_hub)(usb_ctrl_t *, usb_device_t *, usb_completion_callback_t callback, void *callback_ctx);
	// Configure an endpoint.
	int (*configure_ep)(usb_ctrl_t *, usb_device_t *, usb_endpoint_t *, usb_completion_callback_t callback, void *callback_ctx);
	// Execute a control or data transfer.
	int (*xfer)(usb_ctrl_t *, usb_device_t *, usb_xfer_t *);
} usb_ctrl_ops_t;

struct usb_ctrl {
	usb_ctrl_ops_t *ops;
};

int usb_submit_xfer(usb_device_t *dev, usb_xfer_t *xfer);
int usb_control_xfer(usb_device_t *dev, usb_setup_t *setup, void *buffer, usb_completion_callback_t callback, void *ctx);

int usb_get_device_descriptor(usb_device_t *dev, uint8_t desc_type, uint8_t desc_index, void *buffer, uint16_t length, usb_completion_callback_t callback, void *callback_ctx);
int usb_get_class_device_descriptor(usb_device_t *dev, uint8_t desc_type, uint8_t desc_index, void *buffer, uint16_t length, usb_completion_callback_t callback, void *callback_ctx);
int usb_get_interface_descriptor(usb_device_t *dev, uint16_t interface_number, uint8_t desc_type, uint8_t desc_index, void *buffer, uint16_t length, usb_completion_callback_t callback, void *callback_ctx);
int usb_get_endpoint_descriptor(usb_device_t *dev, uint16_t endpoint_address, uint8_t desc_type, uint8_t desc_index, void *buffer, uint16_t length, usb_completion_callback_t callback, void *callback_ctx);
int usb_set_configuration(usb_device_t *dev, uint8_t config_value, usb_completion_callback_t callback);

void usb_hub_event_connect(usb_hub_t *hub, int port);
void usb_hub_event_disconnect(usb_hub_t *hub, int port);
void usb_hub_event_reset(usb_hub_t *hub, int port, usb_speed_t speed);

#define USB_DRIVER_SCORE_NONE 0
#define USB_DRIVER_SCORE_GENERIC 10
#define USB_DRIVER_SCORE_CLASS_MATCH 30
#define USB_DRIVER_SCORE_SUBCLASS_MATCH 50
#define USB_DRIVER_SCORE_PROTOCOL_MATCH 60
#define USB_DRIVER_SCORE_VENDOR_MATCH 80
#define USB_DRIVER_SCORE_PRODUCT_MATCH 90
#define USB_DRIVER_SCORE_EXACT_MATCH 100

#define usb_for_each_descriptor(desc, item) \
	for (usb_desc_hdr_t *item = (usb_desc_hdr_t *)((void *)(desc) + desc->bLength); \
		(void *)item < (void *)(desc) + (desc)->wTotalLength; \
		item = (usb_desc_hdr_t *)((void *)item + item->bLength))

static inline bool usb_descriptor_malformed(usb_config_desc_t *desc, usb_desc_hdr_t *item) {
	uint8_t *top = (uint8_t *)desc + desc->wTotalLength;
	uint8_t *start = (uint8_t *)item;

	if (start + sizeof(*item) > top)
		return true;

	if (item->bLength < sizeof(*item))
		return true;

	return start + item->bLength > top;
}

#define USB_FOR_EACH_DESCRIPTOR_CHECK(desc, item) \
	if (usb_descriptor_malformed((desc), (item)))

struct usb_class_driver {
	const char *name;
	// Probe function, returns score - (0 = won't handle, higher = better match).
	int (*probe)(usb_device_t *dev, usb_interface_t *interface);
	// Called when this driver is selected for the interface.
	int (*attach)(usb_device_t *dev, usb_interface_t *interface);
	// Called when device is disconnected.
	void (*detach)(usb_device_t *dev, usb_interface_t *interface);
};

#define DEFINE_USB_CLASS_DRIVER(name, ...) \
	static usb_class_driver_t __usb_class_driver_##name = { \
		__VA_ARGS__ \
	}; \
	__attribute__((section(".usb_class_drivers"), used)) static usb_class_driver_t *usb_class_driver_##name = &__usb_class_driver_##name;

#endif
