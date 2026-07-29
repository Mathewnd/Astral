#include <kernel/usb.h>
#include <errno.h>
#include <logging.h>
#include <kernel/alloc.h>
#include <kernel/scheduler.h>
#include <string.h>

#define USB_HUB_REQUEST_GET_STATUS 0
#define USB_HUB_REQUEST_CLEAR_FEATURE 1
#define USB_HUB_REQUEST_SET_FEATURE 3

#define USB_HUB_MIN_POWER_GOOD_US 100000

typedef struct {
	uint64_t in_buffer;
	size_t in_bitmap_size;
	usb_hub_t *usb_hub;
	uint32_t *status_buffer;
	usb_endpoint_t *interrupt_in_endpoint;
	size_t ports_powered;
	size_t ports_initialized;
} hub_driver_data_t;

static int hub_port_control_xfer(usb_device_t *dev, uint8_t request, uint16_t value, int port, void *buffer, uint16_t length, usb_completion_callback_t callback, void *callback_ctx) {
	usb_setup_t setup = {0};
	setup.bmRequestType = USB_REQUEST_RECIP_OTHER | USB_REQUEST_CLASS;
	setup.bmRequestType |= buffer ? USB_REQUEST_DIR_TO_HOST : USB_REQUEST_DIR_TO_DEVICE;
	setup.bRequest = request;
	setup.wValue = value;
	setup.wIndex = port + 1;
	setup.wLength = length;

	return usb_control_xfer(dev, &setup, buffer, callback, callback_ctx);
}

static int get_status_cmd(usb_device_t *dev, hub_driver_data_t *data, int port, usb_completion_callback_t callback, void *callback_ctx) {
	if (port < 0 || port >= data->usb_hub->port_count)
		return EINVAL;

	return hub_port_control_xfer(dev, USB_HUB_REQUEST_GET_STATUS, 0, port, &data->status_buffer[port], sizeof(data->status_buffer[port]), callback, callback_ctx);
}

static hub_driver_data_t *hub_get_driver_data(usb_hub_t *hub) {
	usb_device_t *dev = hub->device;
	if (dev == NULL || dev->config_desc == NULL || dev->interfaces == NULL)
		return NULL;

	for (int i = 0; i < dev->config_desc->bNumInterfaces; ++i) {
		hub_driver_data_t *data = dev->interfaces[i].driver_data;
		if (data != NULL && data->usb_hub == hub)
			return data;
	}

	return NULL;
}

static int hub_get_port_status(usb_hub_t *hub, uint8_t port, uint16_t *status, uint16_t *change) {
	if (port >= hub->port_count)
		return EINVAL;

	hub_driver_data_t *data = hub_get_driver_data(hub);
	if (data == NULL)
		return ENODEV;

	uint32_t port_status = data->status_buffer[port];
	if (status)
		*status = port_status & 0xffff;
	if (change)
		*change = port_status >> 16;

	return 0;
}

static void reset_port_callback(usb_device_t *dev, void *ctx, usb_status_t status, size_t transferred) {
	(void)dev;
	(void)transferred;

	if (status != USB_STATUS_SUCCESS)
		printf("hub: failed to reset port %lu\n", (uintptr_t)ctx);
}

static int hub_set_port_feature(usb_hub_t *hub, uint8_t port, uint16_t feature) {
	if (port >= hub->port_count)
		return EINVAL;

	usb_completion_callback_t callback = NULL;
	void *callback_ctx = NULL;
	if (feature == USB_HUB_FEATURE_PORT_RESET) {
		callback = reset_port_callback;
		callback_ctx = (void *)(uintptr_t)(port + 1);
	}

	return hub_port_control_xfer(hub->device, USB_HUB_REQUEST_SET_FEATURE, feature, port, NULL, 0, callback, callback_ctx);
}

static int hub_clear_port_feature(usb_hub_t *hub, uint8_t port, uint16_t feature) {
	if (port >= hub->port_count)
		return EINVAL;

	return hub_port_control_xfer(hub->device, USB_HUB_REQUEST_CLEAR_FEATURE, feature, port, NULL, 0, NULL, NULL);
}

typedef struct {
	int port;
	hub_driver_data_t *data;
} callback_ctx_t;

static void clear_port_changes(hub_driver_data_t *data, int port, uint16_t port_change) {
	if (port_change & USB_HUB_PORT_CHANGE_PORT_CONNECTION) {
		if (data->usb_hub->ops->clear_port_feature(data->usb_hub, port, USB_HUB_FEATURE_C_PORT_CONNECTION))
			printf("hub: failed to clear connection change on port %d\n", port);
	}
	if (port_change & USB_HUB_PORT_CHANGE_PORT_ENABLE) {
		if (data->usb_hub->ops->clear_port_feature(data->usb_hub, port, USB_HUB_FEATURE_C_PORT_ENABLE))
			printf("hub: failed to clear enable change on port %d\n", port);
	}
	if (port_change & USB_HUB_PORT_CHANGE_PORT_OVER_CURRENT) {
		if (data->usb_hub->ops->clear_port_feature(data->usb_hub, port, USB_HUB_FEATURE_C_PORT_OVER_CURRENT))
			printf("hub: failed to clear over-current change on port %d\n", port);
	}
	if (port_change & USB_HUB_PORT_CHANGE_PORT_RESET) {
		if (data->usb_hub->ops->clear_port_feature(data->usb_hub, port, USB_HUB_FEATURE_C_PORT_RESET))
			printf("hub: failed to clear reset change on port %d\n", port);
	}
}

static usb_speed_t speed_from_port_status(usb_hub_t *hub, uint16_t port_status) {
	if (hub->device->speed == USB_SPEED_SUPER || hub->device->speed == USB_SPEED_SUPER_PLUS)
		return USB_SPEED_SUPER;

	if (port_status & USB_HUB_PORT_STATUS_LOW_SPEED)
		return USB_SPEED_LOW;
	if (port_status & USB_HUB_PORT_STATUS_HIGH_SPEED)
		return USB_SPEED_HIGH;

	return USB_SPEED_FULL;
}

static void handle_port_change(hub_driver_data_t *data, int port, uint16_t port_status, uint16_t port_change) {
	if (port_change & USB_HUB_PORT_CHANGE_PORT_CONNECTION) {
		if (port_status & USB_HUB_PORT_STATUS_PORT_CONNECTION) {
			if (data->usb_hub->ports[port].device != NULL)
				usb_hub_event_disconnect(data->usb_hub, port);
			usb_hub_event_connect(data->usb_hub, port);
		} else {
			usb_hub_event_disconnect(data->usb_hub, port);
		}
	} else if (port_change & USB_HUB_PORT_CHANGE_PORT_RESET) {
		usb_hub_event_reset(data->usb_hub, port, speed_from_port_status(data->usb_hub, port_status));
	}
}

static void get_status_callback(usb_device_t *dev, void *ctx, usb_status_t status, size_t transferred) {
	callback_ctx_t *callback_ctx = ctx;
	hub_driver_data_t *data = callback_ctx->data;
	int port = callback_ctx->port;

	if (status != USB_STATUS_SUCCESS) {
		printf("hub: failed to get status of port %d\n", port);
		goto out;
	}

	uint16_t port_status = data->status_buffer[port] & 0xffff;
	uint16_t port_change = data->status_buffer[port] >> 16;

	handle_port_change(data, port, port_status, port_change);
	clear_port_changes(data, port, port_change);

out:
	free(ctx);
}

static int insert_in_transfer(usb_device_t *dev, hub_driver_data_t *data);

static bool hub_change_bitmap_has_port(hub_driver_data_t *data, size_t transferred, int port) {
	size_t bit = port + 1;
	size_t byte = bit / 8;

	if (byte >= transferred || byte >= data->in_bitmap_size || byte >= sizeof(data->in_buffer))
		return false;

	uint8_t *bitmap = (uint8_t *)&data->in_buffer;
	return (bitmap[byte] & (1 << (bit % 8))) != 0;
}

static void interrupt_in_callback(usb_device_t *dev, void *ctx, usb_status_t status, size_t transferred) {
	hub_driver_data_t *data = ctx;
	if (status != USB_STATUS_SUCCESS) {
		printf("hub: IN interrupt failed; retrying\n");
	} else {
		for (int port = 0; port < data->usb_hub->port_count; ++port) {
			if (!hub_change_bitmap_has_port(data, transferred, port))
				continue;

			callback_ctx_t *callback_ctx = alloc(sizeof(callback_ctx_t));
			if (callback_ctx == NULL) {
				printf("hub: out of memory to do get_status\n");
				continue;
			}

			callback_ctx->port = port;
			callback_ctx->data = data;

			if (get_status_cmd(dev, data, port, get_status_callback, callback_ctx)) {
				printf("hub: get_status_cmd() failed\n");
				free(callback_ctx);
			}
		}
	}

	data->in_buffer = 0;
	if (insert_in_transfer(dev, data)) {
		printf("hub: failed to insert in trasnfer during callback. hub will not be functional.\n");
	}
}

static int insert_in_transfer(usb_device_t *dev, hub_driver_data_t *data) {
	usb_xfer_t xfer = {
		.ep = data->interrupt_in_endpoint,
		.flags = USB_XFER_FLAG_TO_HOST,
		.type = USB_XFER_TYPE_INTERRUPT,
		.data = &data->in_buffer,
		.data_length = data->in_bitmap_size,
		.completion = interrupt_in_callback,
		.completion_ctx = data
	};

	int error = usb_submit_xfer(dev, &xfer);
	if (error) {
		printf("hub: failed to submit IN xfer: %s\n", strerror(error));
	}
	return error;
}

static usb_hub_ops_t hub_ops = {
	.get_port_status = hub_get_port_status,
	.set_port_feature = hub_set_port_feature,
	.clear_port_feature = hub_clear_port_feature,
};

static void init_status_get_callback(usb_device_t *dev, void *ctx, usb_status_t status, size_t transferred) {
	hub_driver_data_t *data = ctx;
	size_t port = data->ports_initialized;

	if (status != USB_STATUS_SUCCESS) {
		printf("hub: failed to get first status of port %lu\n", port);
		return;
	}

	uint16_t port_status = data->status_buffer[port] & 0xffff;
	uint16_t port_change = data->status_buffer[port] >> 16;

	if (port_status & USB_HUB_PORT_STATUS_PORT_CONNECTION)
		usb_hub_event_connect(data->usb_hub, port);

	clear_port_changes(data, port, port_change);

	if (++data->ports_initialized != data->usb_hub->port_count) {
		if (get_status_cmd(dev, data, data->ports_initialized, init_status_get_callback, data)) {
			printf("hub: get_status_cmd() failed\n");
		}
	}
}

static int power_port(usb_device_t *dev, int port, hub_driver_data_t *data);

static void initial_status_scan(usb_device_t *dev, hub_driver_data_t *data) {
	if (get_status_cmd(dev, data, 0, init_status_get_callback, data))
		printf("hub: failed to get status of first port\n");
}

static void power_on_callback(usb_device_t *dev, void *ctx, usb_status_t status, size_t transferred) {
	hub_driver_data_t *data = ctx;

	if (status != USB_STATUS_SUCCESS) {
		printf("hub: failed to power on port %lu\n", data->ports_powered);
		return;
	}

	if (++data->ports_powered == data->usb_hub->port_count) {
		usb_hub_desc_t *desc = (usb_hub_desc_t *)dev->hub_desc;
		size_t power_good_us = (size_t)desc->bPwrOn2PwrGood * 2000;
		// External hubs need at least 100 ms for port power to stabilize.
		if (power_good_us < USB_HUB_MIN_POWER_GOOD_US)
			power_good_us = USB_HUB_MIN_POWER_GOOD_US;
		sched_sleep_us(power_good_us);
		initial_status_scan(dev, data);
	} else if (power_port(dev, data->ports_powered, data)) {
		printf("hub: power_port() failed\n");
	}
}

static int power_port(usb_device_t *dev, int port, hub_driver_data_t *data) {
	return hub_port_control_xfer(data->usb_hub->device, USB_HUB_REQUEST_SET_FEATURE, USB_HUB_FEATURE_PORT_POWER, port, NULL, 0, power_on_callback, data);
}

static void get_hub_name(usb_device_t *dev, char *buffer, size_t buffer_size) {
	uint8_t major = (dev->desc.bcdUSB >> 8) & 0x0f;
	uint8_t minor_tens = (dev->desc.bcdUSB >> 4) & 0x0f;
	uint8_t minor_ones = dev->desc.bcdUSB & 0x0f;

	if (minor_ones)
		snprintf(buffer, buffer_size, "usb-hub-%u.%u%u", major, minor_tens, minor_ones);
	else
		snprintf(buffer, buffer_size, "usb-hub-%u.%u", major, minor_tens);
}

static int hub_attach(usb_device_t *dev, usb_interface_t *interface) {
	__assert(dev->hub_desc);
	hub_driver_data_t *hub_driver_data = alloc(sizeof(hub_driver_data_t));
	if (hub_driver_data == NULL)
		return ENOMEM;

	hub_driver_data->usb_hub = alloc(sizeof(usb_hub_t));
	if (hub_driver_data->usb_hub == NULL) {
		free(hub_driver_data);
		return ENOMEM;
	}

	get_hub_name(dev, hub_driver_data->usb_hub->name, sizeof(hub_driver_data->usb_hub->name));
	hub_driver_data->usb_hub->ctrl = dev->hub->ctrl;
	hub_driver_data->usb_hub->ops = &hub_ops;
	hub_driver_data->usb_hub->device = dev;
	hub_driver_data->usb_hub->port_count = ((usb_hub_desc_t *)dev->hub_desc)->bNbrPorts;
	if (hub_driver_data->usb_hub->port_count == 0) {
		free(hub_driver_data->usb_hub);
		free(hub_driver_data);
		return EINVAL;
	}

	hub_driver_data->usb_hub->ports = alloc(sizeof(usb_hub_port_t) * hub_driver_data->usb_hub->port_count);
	if (hub_driver_data->usb_hub->ports == NULL) {
		free(hub_driver_data->usb_hub);
		free(hub_driver_data);
		return ENOMEM;
	}

	hub_driver_data->status_buffer = alloc(sizeof(uint32_t) * hub_driver_data->usb_hub->port_count);
	if (hub_driver_data->status_buffer == NULL) {
		free(hub_driver_data->usb_hub->ports);
		free(hub_driver_data->usb_hub);
		free(hub_driver_data);
		return ENOMEM;
	}

	hub_driver_data->in_bitmap_size = ROUND_UP(hub_driver_data->usb_hub->port_count + 1, 8) / 8;
	if (hub_driver_data->in_bitmap_size > sizeof(hub_driver_data->in_buffer)) {
		printf("hub: too many ports for interrupt bitmap\n");
		free(hub_driver_data->status_buffer);
		free(hub_driver_data->usb_hub->ports);
		free(hub_driver_data->usb_hub);
		free(hub_driver_data);
		return EINVAL;
	}

	for (int i = 0; i < interface->desc->bNumEndpoints; ++i) {
		if (interface->endpoints[i].desc && (interface->endpoints[i].desc->bEndpointAddress & USB_ENDPOINT_ADDRESS_DIR_IN) && (interface->endpoints[i].desc->bmAttributes & USB_ENDPOINT_ATTRIB_TYPE_MASK) == USB_ENDPOINT_ATTRIB_TYPE_INTR)
			hub_driver_data->interrupt_in_endpoint = &interface->endpoints[i];
	}

	if (hub_driver_data->interrupt_in_endpoint == NULL) {
		printf("hub: no IN interrupt endpoint found\n");
		free(hub_driver_data->status_buffer);
		free(hub_driver_data->usb_hub->ports);
		free(hub_driver_data->usb_hub);
		free(hub_driver_data);
		return EINVAL;
	}

	interface->driver_data = hub_driver_data;

	int error = insert_in_transfer(dev, hub_driver_data);
	if (error) {
		printf("hub: failed to insert first IN transfer\n");
		interface->driver_data = NULL;
		free(hub_driver_data->status_buffer);
		free(hub_driver_data->usb_hub->ports);
		free(hub_driver_data->usb_hub);
		free(hub_driver_data);
		return error;
	}

	// Some hubs emulate power switching but still require PORT_POWER requests.
	if (power_port(dev, 0, hub_driver_data))
		printf("hub: failed to power first port\n");

	return 0;
}

static int hub_probe(usb_device_t *dev, usb_interface_t *interface) {
	if (interface->desc->bInterfaceClass == USB_CLASS_HUB)
		return USB_DRIVER_SCORE_CLASS_MATCH;

	return USB_DRIVER_SCORE_NONE;
}

DEFINE_USB_CLASS_DRIVER(hub_class,
	.name = "USB Hub",
	.attach = hub_attach,
	.probe = hub_probe
	// TODO detach
)
