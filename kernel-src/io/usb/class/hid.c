#include <kernel/usb.h>
#include <kernel/alloc.h>
#include <errno.h>
#include <logging.h>
#include <kernel/usb_hid.h>

// TODO proper cleanup

#define USB_INTERFACE_CLASS_HID 0x03

#define USB_DESC_TYPE_HID             0x21
#define USB_DESC_TYPE_HID_REPORT      0x22
#define USB_DESC_TYPE_HID_PHYSICAL    0x23

typedef struct {
        uint8_t bDescriptorType;
        uint16_t wDescriptorLength;
} __attribute__((packed)) usb_hid_class_desc_t;

typedef struct {
        uint8_t bLength;
        uint8_t bDescriptorType;
        uint16_t bcdHID;
        uint8_t bCountryCode;
        uint8_t bNumDescriptors;
        usb_hid_class_desc_t descriptors[];
} __attribute__((packed)) usb_hid_desc_t;


typedef struct {
	uint8_t *report_descriptor;
	size_t report_descriptor_length;
	hid_parser_t parser;
	usb_endpoint_t *interrupt_in_endpoint;
	size_t interrupt_in_packet_size;
	void *data_buffer;
} hid_driver_t;

#define HID_GD_POINTER 0x01
#define HID_GD_MOUSE 0x02
#define HID_GD_JOYSTICK 0x04
#define HID_GD_GAMEPAD 0x05
#define HID_GD_KEYBOARD 0x06
#define HID_GD_KEYPAD 0x07
#define HID_GD_MULTI_AXIS_CONTROLLER 0x08

static inline const char *get_pretty_name_gd(uint32_t usage) {
	switch (USAGE(usage)) {
		case HID_GD_POINTER:
			return "HID Pointer";
		case HID_GD_MOUSE:
			return "HID Mouse";
		case HID_GD_JOYSTICK:
			return "HID Joystick";
		case HID_GD_GAMEPAD:
			return "HID Gamepad";
		case HID_GD_KEYBOARD:
			return "HID Keyboard";
		case HID_GD_KEYPAD:
			return "HID Keypad";
		case HID_GD_MULTI_AXIS_CONTROLLER:
			return "HID Multi Axis Controller";
	}

	return "HID Device (GD)";
}

static usb_hid_desc_t *hid_find_descriptor(usb_device_t *dev, usb_interface_t *interface) {
	bool in_interface = false;

	usb_for_each_descriptor(dev->config_desc, desc) {
		USB_FOR_EACH_DESCRIPTOR_CHECK(dev->config_desc, desc) {
			return NULL;
		}

		if (desc == (usb_desc_hdr_t *)interface->desc) {
			in_interface = true;
			continue;
		}

		if (!in_interface)
			continue;

		if (desc->bDescriptorType == USB_DESCRIPTOR_TYPE_INTERFACE)
			break;

		if (desc->bDescriptorType != USB_DESC_TYPE_HID)
			continue;

		if (desc->bLength < sizeof(usb_hid_desc_t))
			return NULL;

		usb_hid_desc_t *usb_hid_desc = (usb_hid_desc_t *)desc;
		size_t expected_length = sizeof(usb_hid_desc_t) + usb_hid_desc->bNumDescriptors * sizeof(usb_hid_class_desc_t);
		if (desc->bLength < expected_length)
			return NULL;

		return usb_hid_desc;
	}

	return NULL;
}

hid_report_t *hid_get_report_from_id(hid_parser_t *parser, unsigned int report_id) {
	hid_report_t *report = NULL;

	for (size_t i = 0; i < parser->report_count; ++i) {
		if (parser->reports[i].report_id == report_id) {
			report = &parser->reports[i];
			break;
		}
	}

	return report;
}

static void interrupt_in_callback(usb_device_t *dev, void *ctx, usb_status_t status, size_t transferred) {
	hid_driver_t *hid_driver = ctx;

	if (status != USB_STATUS_SUCCESS && status != USB_STATUS_SHORT_PACKET) {
		printf("hid: interrupt transfer failed. retrying\n");
	} else {
		if (transferred == 0)
			goto resend;

		uint8_t *data = hid_driver->data_buffer;
		hid_report_t *report = NULL;
		if (hid_driver->parser.report_count) { 
			report = hid_get_report_from_id(&hid_driver->parser, data[0]);
			if (report == NULL) {
				printf("hid: got unknown report. ignoring\n");
				goto resend;
			}

			++data;
			--transferred;
		}

		hid_parse_report(&hid_driver->parser, report, data, transferred);
	}

resend:
	usb_xfer_t xfer = {
		.ep = hid_driver->interrupt_in_endpoint,
		.flags = USB_XFER_FLAG_TO_HOST,
		.type = USB_XFER_TYPE_INTERRUPT,
		.data = hid_driver->data_buffer,
		.data_length = hid_driver->interrupt_in_packet_size,
		.completion = interrupt_in_callback,
		.completion_ctx = hid_driver
	};

	int error = usb_submit_xfer(dev, &xfer);
	if (error) {
		printf("hid: failed to submit HID interrupt xfer: %s\n", strerror(error));
	}
}

static void get_report_descriptor_callback(usb_device_t *dev, void *ctx, usb_status_t status, size_t transferred) {
	if (status != USB_STATUS_SUCCESS) {
		printf("hid: failed to get report descriptor\n");
		return;
	}

	usb_interface_t *interface = ctx;
	hid_driver_t *hid_driver = interface->driver_data;

	if (transferred != hid_driver->report_descriptor_length) {
		printf("hid: transferred != hid_driver->report_descriptor_length\n");
		return;
	}

	for (int i = 0; i < interface->desc->bNumEndpoints; ++i) {
		if (interface->endpoints[i].desc && (interface->endpoints[i].desc->bEndpointAddress & USB_ENDPOINT_ADDRESS_DIR_IN) && (interface->endpoints[i].desc->bmAttributes & USB_ENDPOINT_ATTRIB_TYPE_MASK) == USB_ENDPOINT_ATTRIB_TYPE_INTR)
			hid_driver->interrupt_in_endpoint = &interface->endpoints[i];
	}

	if (hid_driver->interrupt_in_endpoint == NULL) {
		printf("hid: no IN interrupt endpoint found\n");
		return;
	}

	int error = hid_parser_init(&hid_driver->parser, hid_driver->report_descriptor, hid_driver->report_descriptor_length);
	if (error) {
		printf("hid: failed to parse report descriptor\n");
		return;
	}

	printf("hid: %lu applications with %lu %s%s\n", 
			hid_driver->parser.application_count, 
			hid_driver->parser.report_count ? hid_driver->parser.report_count : hid_driver->parser.input_count, 
			hid_driver->parser.report_count ? "report" : "anonymous input",
			(hid_driver->parser.report_count + hid_driver->parser.input_count != 1) ? "s" : ""
	);

	hid_driver->interrupt_in_packet_size = usb_endpoint_max_packet_size(hid_driver->interrupt_in_endpoint->desc);
	hid_driver->data_buffer = alloc(hid_driver->interrupt_in_packet_size);
	if (hid_driver->data_buffer == NULL) {
		printf("hid: out of memory\n");
		return;
	}

	for (size_t i = 0; i < hid_driver->parser.application_count; ++i) {
		hid_application_t *application = &hid_driver->parser.applications[i];
		const char *pretty_name = "HID Device";
		if (USAGE_PAGE(application->usage) == HID_USAGE_PAGE_GENERIC_DESKTOP)
			pretty_name = get_pretty_name_gd(application->usage);

		application->input_device = input_new();
		if (application->input_device == NULL) {
			printf("hid: failed to create new input device\n");
			return;
		}

		application->input_device->id_bus = INPUT_DEVICE_BUS_USB;
		application->input_device->id_vendor = dev->desc.idVendor;
		application->input_device->id_product = dev->desc.idProduct;
		snprintf(application->input_device->name, sizeof(application->input_device->name), pretty_name);
		application->input_device->id_version = 1;
		application->input_device->ver_major = 1;
		application->input_device->ver_minor = 0;
		application->input_device->ver_patch = 0;
	}

	hid_advertise_events(&hid_driver->parser);

	usb_xfer_t xfer = {
		.ep = hid_driver->interrupt_in_endpoint,
		.flags = USB_XFER_FLAG_TO_HOST,
		.type = USB_XFER_TYPE_INTERRUPT,
		.data = hid_driver->data_buffer,
		.data_length = hid_driver->interrupt_in_packet_size,
		.completion = interrupt_in_callback,
		.completion_ctx = hid_driver
	};

	error = usb_submit_xfer(dev, &xfer);
	if (error) {
		printf("hid: failed to submit initial hid xfer: %s\n", strerror(error));
	}
}

static int hid_attach(usb_device_t *dev, usb_interface_t *interface) {
	usb_hid_desc_t *usb_hid_desc = hid_find_descriptor(dev, interface);
	if (usb_hid_desc == NULL) {
		printf("hid: invalid interface descriptor\n");
		return EINVAL;
	}

	usb_hid_class_desc_t *usb_hid_class_desc = NULL;
	for (int i = 0; i < usb_hid_desc->bNumDescriptors; ++i) {
		if (usb_hid_desc->descriptors[i].bDescriptorType != USB_DESC_TYPE_HID_REPORT)
			continue;

		usb_hid_class_desc = &usb_hid_desc->descriptors[i];
		break;
	}

	if (usb_hid_class_desc == NULL) {
		printf("hid: no report descriptor\n");
		return EINVAL;
	}

	hid_driver_t *hid_driver = alloc(sizeof(hid_driver_t));
	if (hid_driver == NULL) {
		printf("hid: out of memory\n");
		return ENOMEM;
	}

	hid_driver->report_descriptor_length = usb_hid_class_desc->wDescriptorLength;
	if (hid_driver->report_descriptor_length == 0) {
		printf("hid: invalid report descriptor length\n");
		free(hid_driver);
		return EINVAL;
	}

	hid_driver->report_descriptor = alloc(hid_driver->report_descriptor_length);
	if (hid_driver->report_descriptor == NULL) {
		printf("hid: out of memory\n");
		free(hid_driver);
		return ENOMEM;
	}

	interface->driver_data = hid_driver;

	int error = usb_get_interface_descriptor(dev, interface->desc->bInterfaceNumber, USB_DESC_TYPE_HID_REPORT, 0, hid_driver->report_descriptor, hid_driver->report_descriptor_length, get_report_descriptor_callback, interface);
	if (error) {
		interface->driver_data = NULL;
		free(hid_driver->report_descriptor);
		free(hid_driver);
	}

	return error;
}

static int hid_probe(usb_device_t *, usb_interface_t *interface) {

	if (interface->desc->bInterfaceClass == USB_INTERFACE_CLASS_HID)
		return USB_DRIVER_SCORE_CLASS_MATCH;

	return USB_DRIVER_SCORE_NONE;
}

DEFINE_USB_CLASS_DRIVER(hid_driver,
		.name = "USB Human Interface Device",
		.probe = hid_probe,
		.attach = hid_attach
		// TODO detach
);
