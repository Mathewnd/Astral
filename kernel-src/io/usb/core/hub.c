#include <kernel/usb.h>
#include <logging.h>
#include <kernel/alloc.h>

// TODO support using other configurations (for simplicity, we will just pick the first one for now)

extern usb_class_driver_t *usb_class_drivers;
extern usb_class_driver_t *usb_class_drivers_end;

static void endpoint_configured(usb_device_t *dev, void *ctx, usb_status_t status, size_t transferred);

static bool endpoint_desc_valid(usb_device_t *dev, usb_endpoint_desc_t *desc) {
	uint8_t ep_num = desc->bEndpointAddress & USB_ENDPOINT_ADDRESS_NUM_MASK;
	uint8_t ep_type = desc->bmAttributes & USB_ENDPOINT_ATTRIB_TYPE_MASK;
	uint16_t max_packet_size = usb_endpoint_max_packet_size(desc);
	uint8_t high_speed_transactions = (desc->wMaxPacketSize >> 11) & 0x3;

	if (ep_num == 0)
		return false;

	if (max_packet_size == 0)
		return false;

	if (ep_type == USB_ENDPOINT_ATTRIB_TYPE_BULK) {
		if (dev->speed == USB_SPEED_LOW)
			return false;
		if (dev->speed == USB_SPEED_HIGH && high_speed_transactions != 0)
			return false;
	} else if (ep_type == USB_ENDPOINT_ATTRIB_TYPE_INTR) {
		if (desc->bInterval == 0)
			return false;
		if ((dev->speed == USB_SPEED_HIGH ||
			dev->speed == USB_SPEED_SUPER ||
			dev->speed == USB_SPEED_SUPER_PLUS) &&
			desc->bInterval > 16)
			return false;
		if (dev->speed == USB_SPEED_HIGH && high_speed_transactions == 3)
			return false;
	} else {
		return false;
	}

	return true;
}

static bool endpoint_requires_ss_companion(usb_device_t *dev, usb_endpoint_t *endpoint) {
	if (dev->speed != USB_SPEED_SUPER && dev->speed != USB_SPEED_SUPER_PLUS)
		return false;

	return (endpoint->desc->bmAttributes & USB_ENDPOINT_ATTRIB_TYPE_MASK) != USB_ENDPOINT_ATTRIB_TYPE_CONTROL;
}

static uint8_t hub_descriptor_type(usb_device_t *dev) {
	if (dev->speed == USB_SPEED_SUPER || dev->speed == USB_SPEED_SUPER_PLUS)
		return USB_DESCRIPTOR_TYPE_SS_HUB;

	return USB_DESCRIPTOR_TYPE_HUB;
}

static bool device_port_stale(usb_device_t *dev) {
	return dev->hub->ports[dev->port_number].generation != dev->port_generation;
}

static int attach_interface_to_driver(usb_device_t *device, usb_interface_t *interface) {
	int best_score = 0;
	usb_class_driver_t *best_driver = NULL;

	for (usb_class_driver_t **it = &usb_class_drivers; it < &usb_class_drivers_end; ++it) {
		usb_class_driver_t *driver = *it;
		int score = driver->probe(device, interface);

		if (score > best_score) {
			best_score = score;
			best_driver = driver;
		}
	}

	if (best_driver == NULL) {
		printf("usb: no driver found for interface %u\n", interface->desc->bInterfaceNumber);
		return 0;
	}

	if (best_driver->attach(device, interface)) {
		printf("usb: attaching %s port %u addr %u interface %u to driver '%s' failed\n",
			device->hub->name,
			device->port_number + 1,
			device->address,
			interface->desc->bInterfaceNumber,
			best_driver->name);
		return 0;
	}

	interface->driver = best_driver;
	printf("usb: %s port %u addr %u device %04x:%04x interface %u attached to driver '%s' (score: %d)\n",
		device->hub->name,
		device->port_number + 1,
		device->address,
		device->desc.idVendor,
		device->desc.idProduct,
		interface->desc->bInterfaceNumber,
		best_driver->name,
		best_score);
	return 0;
}

// device plug ininitialization is complete, pass control for each interface to a driver
static int attach_to_drivers(usb_device_t *device) {
	if (device_port_stale(device))
		return ENODEV;

	device->hub->ports[device->port_number].device = device;

	for (int i = 0; i < device->config_desc->bNumInterfaces; ++i) {
		usb_interface_t *interface = &device->interfaces[i];

		if (interface->desc == NULL)
			return EINVAL;

		if (attach_interface_to_driver(device, interface))
			return EINVAL;
	}

	return 0;
}

static int configure_next_endpoint(usb_device_t *dev, uint8_t interface_idx, uint8_t endpoint_idx) {
	if (device_port_stale(dev))
		return ENODEV;

	for (; interface_idx < dev->config_desc->bNumInterfaces; interface_idx++, endpoint_idx = 0) {
		usb_interface_t *interface = &dev->interfaces[interface_idx];
		if (interface->desc == NULL)
			return EINVAL;

		if (endpoint_idx < interface->desc->bNumEndpoints) {
			usb_endpoint_t *endpoint = &interface->endpoints[endpoint_idx];
			if (endpoint->desc == NULL)
				return EINVAL;

			uintptr_t id = endpoint_idx | (interface_idx << 8);
			return dev->hub->ctrl->ops->configure_ep(dev->hub->ctrl, dev, endpoint, endpoint_configured, (void *)id);
		}
	}

	return attach_to_drivers(dev);
}

static void post_config_cleanup(usb_device_t *dev) {
	for (int i = 0; i < dev->config_desc->bNumInterfaces; ++i) {
		if (dev->interfaces[i].endpoints)
			free(dev->interfaces[i].endpoints);
	}

	if (dev->hub_desc)
		free(dev->hub_desc);
	free(dev->interfaces);
	free(dev->config_desc);
	dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
}

// configure all the endpoints and attach to driver if the last endpoint was configured
static void endpoint_configured(usb_device_t *dev, void *ctx, usb_status_t status, size_t transferred) {
	if (status != USB_STATUS_SUCCESS)
		goto error;

	if (device_port_stale(dev))
		goto error;

	uintptr_t id = (uintptr_t)ctx;
	uint8_t endpoint_idx = (id & 0xff) + 1;
	uint8_t interface_idx = (id >> 8) & 0xff;

	if (configure_next_endpoint(dev, interface_idx, endpoint_idx) == 0)
		return; // success, wait for endpoint to be configured.

	error:
	printf("usb: endpoint config failed\n");
	post_config_cleanup(dev);
}

static void marked_as_hub(usb_device_t *dev, void *, usb_status_t status, size_t) {
	if (status != USB_STATUS_SUCCESS) {
		printf("usb: failed to mark device as hub\n");
		post_config_cleanup(dev);
		return;
	}

	if (device_port_stale(dev)) {
		post_config_cleanup(dev);
		return;
	}

	if (configure_next_endpoint(dev, 0, 0)) {
		printf("usb: failed to configure first endpoint\n");
		post_config_cleanup(dev);
		return;
	}
}

static void get_hub_desc_done(usb_device_t *dev, void *, usb_status_t status, size_t) {
	if (status != USB_STATUS_SUCCESS) {
		printf("usb: failed to get full hub descriptor\n");
		post_config_cleanup(dev);
		return;
	}

	if (device_port_stale(dev)) {
		post_config_cleanup(dev);
		return;
	}

	if (dev->hub->ctrl->ops->mark_as_hub) {
		if (dev->hub->ctrl->ops->mark_as_hub(dev->hub->ctrl, dev, marked_as_hub, NULL)) {
			printf("usb: mark_as_hub failed\n");
			post_config_cleanup(dev);
		}
	} else {
		if (configure_next_endpoint(dev, 0, 0)) {
			printf("usb: failed to configure first endpoint of device\n");
			post_config_cleanup(dev);
		}
	}
}

static void get_hub_desc_header_done(usb_device_t *dev, void *, usb_status_t status, size_t transferred) {
	if (status != USB_STATUS_SUCCESS) {
		printf("usb: failed to get hub descriptor header\n");
		post_config_cleanup(dev);
		return;
	}

	if (device_port_stale(dev)) {
		post_config_cleanup(dev);
		return;
	}

	void *p = realloc(dev->hub_desc, dev->hub_desc->bLength);
	if (p == NULL) {
		printf("usb: out of memory to get full hub descriptor\n");
		post_config_cleanup(dev);
		return;
	}
	dev->hub_desc = p;

	if (usb_get_class_device_descriptor(dev, hub_descriptor_type(dev), 0, dev->hub_desc, dev->hub_desc->bLength, get_hub_desc_done, NULL)) {
		printf("usb: usb_get_class_device_descriptor failed\n");
		post_config_cleanup(dev);
	}
}

// allocate and populate interface and endpoint datastructures, then start configuring endpoints
static void set_configuration_done(usb_device_t *dev, void *, usb_status_t status, size_t transferred) {
	if (status != USB_STATUS_SUCCESS) {
		free(dev->config_desc);
		dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
		printf("usb: failed to set configuration\n");
		return;
	}

	if (device_port_stale(dev)) {
		free(dev->config_desc);
		dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
		return;
	}

	dev->interfaces = alloc(dev->config_desc->bNumInterfaces * sizeof(usb_interface_t));
	if (dev->interfaces == NULL) {
		free(dev->config_desc);
		dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
		printf("usb: out of memory\n");
		return;
	}

	bool has_hub = dev->desc.bDeviceClass == USB_CLASS_HUB;
	// set up interface data
	usb_for_each_descriptor(dev->config_desc, desc) {
		// since this is the first pass over the configure descriptor,
		// we will verify if it is valid and fail the enumeration if not
		USB_FOR_EACH_DESCRIPTOR_CHECK(dev->config_desc, desc) {
			printf("usb: malformed config description\n");
			goto interface_setup_error;
		}

		if (desc->bDescriptorType != USB_DESCRIPTOR_TYPE_INTERFACE)
			continue;

		if (desc->bLength < sizeof(usb_interface_desc_t)) {
			printf("usb: malformed interface description\n");
			goto interface_setup_error;
		}

		usb_interface_desc_t *interface_desc = (usb_interface_desc_t *)desc;
		if (interface_desc->bAlternateSetting)
			continue;

		if (interface_desc->bInterfaceNumber >= dev->config_desc->bNumInterfaces) {
			printf("usb: malformed interface description\n");
			goto interface_setup_error;
		}

		usb_interface_t *interface = &dev->interfaces[interface_desc->bInterfaceNumber];
		if (interface->desc != NULL) {
			printf("usb: duplicate interface description\n");
			goto interface_setup_error;
		}

		if (interface_desc->bInterfaceClass == USB_CLASS_HUB)
			has_hub = true;

		interface->desc = interface_desc;
		if (interface_desc->bNumEndpoints > 0) {
			interface->endpoints = alloc(interface_desc->bNumEndpoints * sizeof(usb_endpoint_t));
			if (interface->endpoints == NULL) {
				printf("usb: out of memory\n");
				goto interface_setup_error;
			}
		}
	}

	// check for missing interfaces
	for (int i = 0; i < dev->config_desc->bNumInterfaces; ++i) {
		if (dev->interfaces[i].desc == NULL) {
			printf("usb: missing interface description\n");
			goto interface_setup_error;
		}
	}

	// set up endpoint data
	usb_interface_t *current_interface = NULL;
	size_t current_endpoint = 0;
	usb_for_each_descriptor(dev->config_desc, desc) {
		USB_FOR_EACH_DESCRIPTOR_CHECK(dev->config_desc, desc) {
			printf("usb: malformed config description\n");
			goto interface_setup_error;
		}

		if (desc->bDescriptorType == USB_DESCRIPTOR_TYPE_INTERFACE) {
			if (desc->bLength < sizeof(usb_interface_desc_t)) {
				printf("usb: malformed interface description\n");
				goto interface_setup_error;
			}

			usb_interface_desc_t *interface_desc = (usb_interface_desc_t *)desc;
			if (interface_desc->bInterfaceNumber >= dev->config_desc->bNumInterfaces) {
				printf("usb: malformed interface description\n");
				goto interface_setup_error;
			}

			if (interface_desc->bAlternateSetting) {
				current_endpoint = 0;
				current_interface = NULL;
				continue;
			}

			current_endpoint = 0;
			current_interface = &dev->interfaces[interface_desc->bInterfaceNumber];
			continue;
		}

		if (desc->bDescriptorType == USB_DESCRIPTOR_TYPE_ENDPOINT) {
			// skip any endpoints on an alternate setting
			if (current_interface == NULL)
				continue;

			if (desc->bLength < sizeof(usb_endpoint_desc_t)) {
				printf("usb: malformed endpoint description\n");
				goto interface_setup_error;
			}

			if (current_endpoint >= current_interface->desc->bNumEndpoints) {
				printf("usb: malformed interface description\n");
				goto interface_setup_error;
			}

			usb_endpoint_desc_t *endpoint_desc = (usb_endpoint_desc_t *)desc;
			if (!endpoint_desc_valid(dev, endpoint_desc)) {
				printf("usb: malformed endpoint description\n");
				goto interface_setup_error;
			}

			current_interface->endpoints[current_endpoint++].desc = endpoint_desc;
		} else if (desc->bDescriptorType == USB_DESCRIPTOR_TYPE_SS_EP_COMPANION) {
			if (current_interface == NULL || current_endpoint == 0 || desc->bLength < sizeof(usb_ss_ep_companion_desc_t)) {
				printf("usb: malformed endpoint companion description\n");
				goto interface_setup_error;
			}

			current_interface->endpoints[current_endpoint - 1].ss_companion = (usb_ss_ep_companion_desc_t *)desc;
		}
	}

	// verify if we got all advertised endpoints
	for (int i = 0; i < dev->config_desc->bNumInterfaces; ++i) {
		usb_interface_t *interface = &dev->interfaces[i];
		for (int j = 0; j < interface->desc->bNumEndpoints; ++j) {
			if (interface->endpoints[j].desc == NULL) {
				printf("usb: missing endpoint description\n");
				goto interface_setup_error;
			}

			if (endpoint_requires_ss_companion(dev, &interface->endpoints[j]) && interface->endpoints[j].ss_companion == NULL) {
				printf("usb: missing endpoint companion description\n");
				goto interface_setup_error;
			}
		}
	}

	if (has_hub) {
		// to set up the context in xhci
		dev->hub_desc = alloc(sizeof(usb_desc_hdr_t));
		if (dev->hub_desc == NULL) {
			printf("usb: out of memory to get hub descriptor header\n");
			post_config_cleanup(dev);
			return;
		}

		if (usb_get_class_device_descriptor(dev, hub_descriptor_type(dev), 0, dev->hub_desc, sizeof(usb_desc_hdr_t), get_hub_desc_header_done, NULL) == 0)
			return;

		printf("usb: usb_get_class_device_descriptor failed\n");
	} else if (configure_next_endpoint(dev, 0, 0) == 0) {
			return; // success, wait for endpoint to be configured.
	}

interface_setup_error:
	post_config_cleanup(dev);
}

// set default configuration
static void get_configuration_done(usb_device_t *dev, void *, usb_status_t status, size_t transferred) {
	if (status != USB_STATUS_SUCCESS) {
		free(dev->config_desc);
		dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
		printf("usb: failed to get config descriptor\n");
		return;
	}

	if (device_port_stale(dev)) {
		free(dev->config_desc);
		dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
		return;
	}

	if (usb_set_configuration(dev, dev->config_desc->bConfigurationValue, set_configuration_done)) {
		free(dev->config_desc);
		dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
		printf("usb: usb_set_configuration failed\n");
	}
}

static bool config_desc_header_valid(usb_config_desc_t *desc, size_t transferred) {
	if (transferred < sizeof(usb_config_desc_t))
		return false;

	if (desc->bLength != sizeof(usb_config_desc_t))
		return false;

	if (desc->bDescriptorType != USB_DESCRIPTOR_TYPE_CONFIG)
		return false;

	if (desc->wTotalLength < desc->bLength)
		return false;

	if (desc->wTotalLength > USB_MAX_CONFIG_DESC_SIZE)
		return false;

	return true;
}

// get the rest of configuration
static void get_configuration_header_done(usb_device_t *dev, void *, usb_status_t status, size_t transferred) {
	if (status != USB_STATUS_SUCCESS) {
		free(dev->config_desc);
		dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
		printf("usb: usb_set_configuration failed\n");
		return;
	}

	if (device_port_stale(dev)) {
		free(dev->config_desc);
		dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
		return;
	}

	if (!config_desc_header_valid(dev->config_desc, transferred)) {
		free(dev->config_desc);
		dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
		printf("usb: malformed config descriptor header\n");
		return;
	}

	void *p = realloc(dev->config_desc, dev->config_desc->wTotalLength);
	if (p == NULL) {
		free(dev->config_desc);
		dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
		printf("usb: out of memory to get configuration\n");
		return;
	}
	dev->config_desc = p;

	if (usb_get_device_descriptor(dev, USB_DESCRIPTOR_TYPE_CONFIG, 0, dev->config_desc, dev->config_desc->wTotalLength, get_configuration_done, NULL)) {
		free(dev->config_desc);
		dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
		printf("usb: usb_get_device_descriptor failed\n");
	}
}

// get configuration descriptor size
static void address_done(usb_hub_t *hub, uint8_t port, usb_device_t *dev) {
	if (dev == NULL) {
		printf("usb: failed to address device in plug-in path\n");
		return;
	}

	if (device_port_stale(dev)) {
		dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
		return;
	}

	dev->config_desc = alloc(sizeof(usb_config_desc_t));
	if (dev->config_desc == NULL) {
		printf("usb: out of memory to get configuration\n");
		dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
		return;
	}

	if (usb_get_device_descriptor(dev, USB_DESCRIPTOR_TYPE_CONFIG, 0, dev->config_desc, sizeof(usb_config_desc_t), get_configuration_header_done, NULL)) {
		printf("usb: usb_get_device_descriptor failed\n");
		free(dev->config_desc);
		dev->hub->ctrl->ops->deaddress_device(dev->hub->ctrl, dev);
	}
}

// address device
void usb_hub_event_reset(usb_hub_t *hub, int port, usb_speed_t speed) {
	if (hub->ctrl->ops->address_device(hub->ctrl, hub, port, speed, address_done)) {
		printf("usb_hub %s: failed to address device\n", hub->name);
	}
}

// reset device
void usb_hub_event_connect(usb_hub_t *hub, int port) {
	__assert(port < hub->port_count);
	hub->ports[port].generation++;

	if (hub->ops->set_port_feature(hub, port, USB_HUB_FEATURE_PORT_RESET)) {
		printf("usb_hub %s: port reset failed\n", hub->name);
	}
}

void usb_hub_event_disconnect(usb_hub_t* hub, int port) {
	__assert(port < hub->port_count);
	hub->ports[port].generation++;

	usb_device_t *device = hub->ports[port].device;
	if (device == NULL) {
		printf("usb: unplugged device during device init\n");
		return;
	}

	hub->ports[port].device = NULL;

	// TODO in the case of a hub driver this would also cause a sort of "recursive disconnect" from there
	for (int i = 0; i < device->config_desc->bNumInterfaces; ++i) {
		usb_interface_t *interface = &device->interfaces[i];
		if (interface->driver && interface->driver->detach)
			interface->driver->detach(device, interface);
	}

	// TODO block new transfers
	// TODO stop transfers

	post_config_cleanup(device);
}
