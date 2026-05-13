#include <kernel/xhci.h>
#include <kernel/alloc.h>
#include <errno.h>
#include <kernel/pmm.h>
#include <logging.h>
#include <util.h>

static bool usb_speed_to_xhci_speed(usb_speed_t speed, uint8_t *out) {
        switch (speed) {
		case USB_SPEED_FULL:
			*out = XHCI_PORT_SPEED_FULL;
			return true;
		case USB_SPEED_LOW:
			*out = XHCI_PORT_SPEED_LOW;
			return true;
		case USB_SPEED_HIGH:
			*out = XHCI_PORT_SPEED_HIGH;
			return true;
		case USB_SPEED_SUPER:
			*out = XHCI_PORT_SPEED_SUPER;
			return true;
		default:
			return false;
        }
}

static usb_speed_t xhci_speed_to_usb_speed(uint8_t speed) {
        switch (speed) {
		case XHCI_PORT_SPEED_FULL:
			return USB_SPEED_FULL;
		case XHCI_PORT_SPEED_LOW:
			return USB_SPEED_LOW;
		case XHCI_PORT_SPEED_HIGH:
			return USB_SPEED_HIGH;
		case XHCI_PORT_SPEED_SUPER:
			return USB_SPEED_SUPER;
		case XHCI_PORT_SPEED_SS_2X1:
		case XHCI_PORT_SPEED_SS_1X2:
		case XHCI_PORT_SPEED_SS_2X2:
			return USB_SPEED_SUPER_PLUS;
		default:
			return USB_SPEED_UNKNOWN;
        }
}

static uint16_t xhci_ep0_max_packet_size(usb_device_t *device) {
	if (device->speed == USB_SPEED_SUPER || device->speed == USB_SPEED_SUPER_PLUS)
		return 1 << device->desc.bMaxPacketSize0;
	return device->desc.bMaxPacketSize0;
}

#define CTX_PTR(CTRL, CTX, INDEX) ((uintptr_t)(CTX) + ((CTRL)->ctx_stride * (INDEX)))

static volatile xhci_input_ctx_t *xhci_get_input_ctrl_ctx(xhci_ctrl_t *xhci, xhci_device_t *dev) {
	return (volatile xhci_input_ctx_t *)CTX_PTR(xhci, dev->input_ctx, 0);
}

static volatile xhci_slot_ctx_t *xhci_get_input_slot_ctx(xhci_ctrl_t *xhci, xhci_device_t *dev) {
	return (volatile xhci_slot_ctx_t *)CTX_PTR(xhci, dev->input_ctx, 1);
}

static volatile xhci_slot_ctx_t *xhci_get_device_slot_ctx(xhci_ctrl_t *xhci, xhci_device_t *dev) {
	return (volatile xhci_slot_ctx_t *)CTX_PTR(xhci, dev->device_ctx, 0);
}

static volatile xhci_ep_ctx_t *xhci_get_input_ep_ctx(xhci_ctrl_t *xhci, xhci_device_t *dev, uint8_t ep) {
	return (volatile xhci_ep_ctx_t *)CTX_PTR(xhci, dev->input_ctx, ep + 2);
}

typedef struct {
	usb_completion_callback_t callback;
	void *callback_ctx;
	uint32_t ep_index;
} xhci_configure_ep_ctx_t;

static void xhci_release_ep_ring(xhci_device_t *dev, uint32_t ep_index) {
	__assert(ep_index > 0 && ep_index <= 31);
	xhci_ring_t *ring = &dev->ep_rings[ep_index - 1];
	xhci_free_ring(ring);
	memset(ring, 0, sizeof(*ring));
}

static void xhci_update_input_context(xhci_ctrl_t *ctrl, xhci_device_t *dev) {
	uint8_t *input_ctx = dev->input_ctx;
	uint8_t *device_ctx = dev->device_ctx;

	// Copy over the slot context and all endpoint contexts.
	memcpy(input_ctx + ctrl->ctx_stride, device_ctx, ctrl->ctx_stride * (1 + 31));
	// Clear input context header.
	memset(input_ctx, 0, sizeof(uint32_t) * 2);
}

static int xhci_update_hub_slot_context(usb_device_t *dev, volatile xhci_slot_ctx_t *slot_ctx) {
	if (dev->hub_desc == NULL)
		return 0;

	slot_ctx->dw0.hub = 1;

	if (dev->hub_desc->bDescriptorType == USB_DESCRIPTOR_TYPE_SS_HUB) {
		if (dev->hub_desc->bLength < sizeof(usb_ss_hub_desc_t))
			return EINVAL;

		usb_ss_hub_desc_t *hub_desc = (usb_ss_hub_desc_t *)dev->hub_desc;
		slot_ctx->dw0.mtt = 0;
		slot_ctx->dw1.number_of_ports = hub_desc->bNbrPorts;
		return 0;
	}

	if (dev->hub_desc->bDescriptorType == USB_DESCRIPTOR_TYPE_HUB) {
		if (dev->hub_desc->bLength < sizeof(usb_hub_desc_t))
			return EINVAL;

		usb_hub_desc_t *hub_desc = (usb_hub_desc_t *)dev->hub_desc;
		slot_ctx->dw0.mtt = dev->desc.bDeviceProtocol == USB_HUB_PROTOCOL_MULTI_TT;
		slot_ctx->dw1.number_of_ports = hub_desc->bNbrPorts;
		return 0;
	}

	return EINVAL;
}

static void xhci_release_device_resources(xhci_ctrl_t *xhci, xhci_device_t *dev) {
	for (int i = 0; i < 31; ++i) {
		if (dev->ep_rings[i].ring != NULL)
			xhci_release_ep_ring(dev, i + 1);
	}

	if (dev->slot_id > 0 && dev->slot_id <= xhci->slot_count) {
		xhci->dcbaa[dev->slot_id] = 0;
		if (xhci->slots[dev->slot_id - 1] == dev)
			xhci->slots[dev->slot_id - 1] = NULL;
	}

	if (dev->input_ctx_phys)
		pmm_release(dev->input_ctx_phys);
	if (dev->device_ctx_phys)
		pmm_release(dev->device_ctx_phys);

	free(dev);
}

static int xhci_disable_slot(xhci_ctrl_t *xhci, xhci_device_t *dev, usb_completion_callback_t callback, void *callback_ctx) {
	xhci_submission_t xhci_submission = {
		.callback = callback,
		.callback_ctx = callback_ctx,
		.device = &dev->device,
	};

	xhci_trb_t trb = {
		.dw3 = XHCI_TRB_DW3_TYPE(TRB_DISABLE_SLOT) | (dev->slot_id << 24)
	};

	xhci_ring_submit(&xhci->command_ring, &trb, &xhci_submission);
	xhci_ring_command_doorbell(xhci);
	return 0;
}

static void address_device_failed_cleanup_callback(usb_device_t *device, void *ctx, usb_status_t status, size_t) {
	usb_address_device_callback_t callback = ctx;
	xhci_device_t *xhci_dev = container_of(device, xhci_device_t, device);
	xhci_ctrl_t *xhci = (xhci_ctrl_t *)xhci_dev->device.hub->ctrl;
	usb_hub_t *hub = device->hub;
	uint8_t port = device->port_number;

	if (status != USB_STATUS_SUCCESS) {
		printf("xhci: disable slot %u failed during address cleanup\n", xhci_dev->slot_id);
		// If Disable Slot fails, the controller may still reference this device's
		// contexts or transfer rings. Leak them for now instead of freeing memory
		// that hardware may still own.
	} else {
		xhci_release_device_resources(xhci, xhci_dev);
	}

	callback(hub, port, NULL);
}

static void address_device_failed_cleanup(usb_device_t *device, usb_address_device_callback_t callback) {
	xhci_device_t *xhci_dev = container_of(device, xhci_device_t, device);
	xhci_ctrl_t *xhci = (xhci_ctrl_t *)xhci_dev->device.hub->ctrl;

	xhci_disable_slot(xhci, xhci_dev, address_device_failed_cleanup_callback, callback);
}

static void address_device_failed_before_slot(xhci_ctrl_t *xhci, xhci_device_t *dev, usb_address_device_callback_t callback) {
	usb_hub_t *hub = dev->device.hub;
	uint8_t port = dev->device.port_number;

	xhci_release_device_resources(xhci, dev);
	callback(hub, port, NULL);
}

static void address_device_evaluate_ctx_callback(usb_device_t *device, void *ctx, usb_status_t status, size_t) {
	usb_address_device_callback_t callback = ctx;
	xhci_device_t *xhci_dev = container_of(device, xhci_device_t, device);
	xhci_ctrl_t *xhci = (xhci_ctrl_t *)xhci_dev->device.hub->ctrl;

	if (status != USB_STATUS_SUCCESS) {
		printf("xhci: evaluate context failed on slot %u\n", xhci_dev->slot_id);
		address_device_failed_cleanup(device, callback);
		return;
	}
	// Update the input context again.
	xhci_update_input_context(xhci, xhci_dev);

	// Tell the original caller that the device has been addresses
	callback(xhci_dev->device.hub, xhci_dev->device.port_number, &xhci_dev->device);
}

static void address_device_get_descriptor_callback(usb_device_t *device, void *ctx, usb_status_t status, size_t transferred) {
	usb_address_device_callback_t callback = ctx;
	xhci_device_t *xhci_dev = container_of(device, xhci_device_t, device);
	xhci_ctrl_t *xhci = (xhci_ctrl_t *)xhci_dev->device.hub->ctrl;

	if (status != USB_STATUS_SUCCESS) {
		printf("xhci: failed to get device descriptor on slot %u\n", xhci_dev->slot_id);
		address_device_failed_cleanup(device, callback);
		return;
	}

	if (!usb_device_desc_valid(&device->desc, device->speed, transferred)) {
		printf("xhci: malformed device descriptor\n");
		address_device_failed_cleanup(device, callback);
		return;
	}

	// Update EP0 context with correct max packet size.
	volatile xhci_ep_ctx_t *ep0_ctx = xhci_get_input_ep_ctx(xhci, xhci_dev, 0);
	volatile xhci_input_ctx_t *input_ctx = xhci_get_input_ctrl_ctx(xhci, xhci_dev);

	ep0_ctx->dw1.max_packet_size = xhci_ep0_max_packet_size(device);

	input_ctx->a |= (1 << 1);

	// Submit an Evaluate Context command.
	xhci_submission_t xhci_submission = {
		.callback = address_device_evaluate_ctx_callback,
		.callback_ctx = callback,
		.device = &xhci_dev->device,
	};

	xhci_trb_t trb = {
		.parameters = (uint64_t)xhci_dev->input_ctx_phys,
		.dw3 = XHCI_TRB_DW3_TYPE(TRB_EVALUATE_CTX) | (xhci_dev->slot_id << 24)
	};

	xhci_ring_submit(&xhci->command_ring, &trb, &xhci_submission);
	xhci_ring_command_doorbell(xhci);
}

static void address_device_addressed_callback(usb_device_t *device, void *ctx, usb_status_t status, size_t) {
	usb_address_device_callback_t callback = ctx;
	xhci_device_t *xhci_dev = container_of(device, xhci_device_t, device);
	xhci_ctrl_t *xhci = (xhci_ctrl_t *)xhci_dev->device.hub->ctrl;

	if (status != USB_STATUS_SUCCESS) {
		printf("xhci: address device failed on slot %d failed\n", xhci_dev->slot_id);
		address_device_failed_cleanup(device, callback);
		return;
	}

	printf("xhci: addressed device on slot %u\n", xhci_dev->slot_id);

	volatile xhci_slot_ctx_t *slot_ctx = xhci_get_device_slot_ctx(xhci, xhci_dev);

	// Update the input context and set the device address.
	xhci_dev->device.address = slot_ctx->dw3.usb_device_address;
	xhci_update_input_context(xhci, xhci_dev);

	if (usb_get_device_descriptor(
				&xhci_dev->device, 
				USB_DESCRIPTOR_TYPE_DEVICE, 
				0, 
				&xhci_dev->device.desc, 
				sizeof(xhci_dev->device.desc), 
				address_device_get_descriptor_callback,
				callback
				)) {
		printf("xhci: usb_get_device_descriptor failed\n");
		address_device_failed_cleanup(device, callback);
	}
}

static void address_device_create_slot_callback(usb_device_t *device, void *ctx, usb_status_t status, size_t slot_id) {
	usb_address_device_callback_t callback = ctx;
	xhci_device_t *xhci_dev = container_of(device, xhci_device_t, device);
	xhci_ctrl_t *xhci = (xhci_ctrl_t *)xhci_dev->device.hub->ctrl;

	if (status != USB_STATUS_SUCCESS) {
		printf("xhci: create slot failed\n");
		address_device_failed_before_slot(xhci, xhci_dev, callback);
		return;
	}

	xhci_dev->slot_id = slot_id;
	__assert(xhci_dev->slot_id > 0 && xhci_dev->slot_id <= xhci->slot_count);
	printf("xhci: allocated slot %u for device %s:%u\n", xhci_dev->slot_id, xhci_dev->device.hub->name, xhci_dev->device.port_number);

	// Set up the Device Context Base Address Array entry.
	xhci->dcbaa[xhci_dev->slot_id] = (uint64_t)xhci_dev->device_ctx_phys;
	xhci->slots[xhci_dev->slot_id - 1] = xhci_dev;

	// Set up the input context and address the device.
	volatile xhci_input_ctx_t *input_ctx = xhci_get_input_ctrl_ctx(xhci, xhci_dev);
	volatile xhci_slot_ctx_t *slot_ctx = xhci_get_input_slot_ctx(xhci, xhci_dev);
	volatile xhci_ep_ctx_t *ep0_ctx = xhci_get_input_ep_ctx(xhci, xhci_dev, 0);

	// Enable slot and EP0 contexts.
	input_ctx->a |= (1 << 0) | (1 << 1);

	// Set up slot context.

	slot_ctx->dw0.route_string = xhci_dev->route_string;
	slot_ctx->dw0.speed = xhci_dev->ctrl_speed;
	slot_ctx->dw0.ctx_entries = 1;
	slot_ctx->dw1.root_hub_port_number = xhci_dev->root_port_number;
	if (device->hub->device != NULL &&
		(device->speed == USB_SPEED_LOW || device->speed == USB_SPEED_FULL) &&
		device->hub->device->speed == USB_SPEED_HIGH) {
		xhci_device_t *parent = container_of(device->hub->device, xhci_device_t, device);
		slot_ctx->dw2.tt_hub_slot_id = parent->slot_id;
		slot_ctx->dw2.tt_port_number = device->port_number + 1;
		slot_ctx->dw2.ttt = 0;
	}

	// Set up EP0 context.
	ep0_ctx->dw1.ep_type = XHCI_EP_TYPE_CTRL;
	ep0_ctx->dw1.cerr = 3;

	if (xhci_dev->ctrl_speed == XHCI_PORT_SPEED_LOW || xhci_dev->ctrl_speed == XHCI_PORT_SPEED_FULL)
		ep0_ctx->dw1.max_packet_size = 8;
	else if (xhci_dev->ctrl_speed == XHCI_PORT_SPEED_HIGH)
		ep0_ctx->dw1.max_packet_size = 64;
	else
		ep0_ctx->dw1.max_packet_size = 512;

	uint64_t ep0_ring_phys = (uint64_t)xhci_dev->ep_rings[0].ring_phys;
	ep0_ctx->dw2.dcs = xhci_dev->ep_rings[0].cycle;
	ep0_ctx->dw2.tr_dequeue_pointer_lo = (uint32_t)((ep0_ring_phys >> 4) & 0xffffffff);
	ep0_ctx->dw3.tr_dequeue_pointer_hi = (uint32_t)(ep0_ring_phys >> 32);

	xhci_submission_t xhci_submission = {
		.callback = address_device_addressed_callback,
		.callback_ctx = callback,
		.device = &xhci_dev->device
	};

	xhci_trb_t trb = {
		.parameters = (uint64_t)xhci_dev->input_ctx_phys,
		.dw3 = XHCI_TRB_DW3_TYPE(TRB_ADDRESS_DEVICE) | (xhci_dev->slot_id << 24)
	};

	xhci_ring_submit(&xhci->command_ring, &trb, &xhci_submission);
	xhci_ring_command_doorbell(xhci);
}

static int xhci_ctrl_address_device(usb_ctrl_t *ctrl, usb_hub_t *hub, uint8_t port, usb_speed_t speed, usb_address_device_callback_t callback) {
	++port; // convert to the format xhci expects

	xhci_ctrl_t *xhci = container_of(ctrl, xhci_ctrl_t, ctrl);

	xhci_device_t *xhci_dev = alloc(sizeof(xhci_device_t));
	if (xhci_dev == NULL)
		return ENOMEM;

	void *device_ctx_phys = pmm_allocpage(PMM_SECTION_DEFAULT);
	if (device_ctx_phys == NULL) {
		free(xhci_dev);
		return ENOMEM;
	}

	void *input_ctx_phys = pmm_allocpage(PMM_SECTION_DEFAULT);
	if (input_ctx_phys == NULL) {
		pmm_release(device_ctx_phys);
		free(xhci_dev);
		return ENOMEM;
	}

	if (xhci_alloc_ring(xhci, &xhci_dev->ep_rings[0], false)) {
		pmm_release(input_ctx_phys);
		pmm_release(device_ctx_phys);
		free(xhci_dev);
		return ENOMEM;
	}

	xhci_dev->device_ctx_phys = device_ctx_phys;
	xhci_dev->device_ctx = MAKE_HHDM(device_ctx_phys);
	memset(xhci_dev->device_ctx, 0, PAGE_SIZE);

	xhci_dev->input_ctx_phys = input_ctx_phys;
	xhci_dev->input_ctx = MAKE_HHDM(input_ctx_phys);
	memset(xhci_dev->input_ctx, 0, PAGE_SIZE);

	xhci_dev->device.hub = hub;
	xhci_dev->device.port_number = port - 1;
	xhci_dev->device.port_generation = hub->ports[port - 1].generation;

	if (hub->device) {
		xhci_device_t *parent = container_of(hub->device, xhci_device_t, device);
		xhci_dev->device_tier = parent->device_tier + 1;
		xhci_dev->route_string = parent->route_string | (port << (parent->device_tier * 4));

		xhci_device_t *xhci_hub_device = (xhci_device_t *)hub->device;
		xhci_dev->root_port_number = xhci_hub_device->root_port_number;
	} else {
		xhci_dev->device_tier = 0;
		xhci_dev->route_string = 0;
		xhci_dev->root_port_number = port;
	}

	if (hub->device == NULL) {
		// this is a root hub! we can just take the speed directly from the portsc register
		xhci_root_hub_t *rh = container_of(hub, xhci_root_hub_t, hub);
		uint32_t portsc = xhci->portregs[rh->port_offset + port - 1].portsc;
		uint32_t port_speed = (portsc >> 10) & 0xf;
		xhci_dev->ctrl_speed = port_speed;
		xhci_dev->device.speed = xhci_speed_to_usb_speed(port_speed);
	} else {
		xhci_dev->device.speed = speed;
		if (!usb_speed_to_xhci_speed(xhci_dev->device.speed, &xhci_dev->ctrl_speed)) {
			// This is a super speed plus device behind a hub.
			// We can't know its speed in advance, so set it as super speed and figure it out later.
			xhci_dev->ctrl_speed = XHCI_PORT_SPEED_SUPER;
		}
	}

	xhci_submission_t xhci_submission = {
		.callback = address_device_create_slot_callback,
		.callback_ctx = callback,
		.device = &xhci_dev->device
	};

	xhci_trb_t trb = {
		.dw3 = XHCI_TRB_DW3_TYPE(TRB_ENABLE_SLOT)
	};

	xhci_ring_submit(&xhci->command_ring, &trb, &xhci_submission);
	xhci_ring_command_doorbell(xhci);
	return 0;
}

static void configure_ep_callback(usb_device_t *dev, void *ctx, usb_status_t status, size_t transferred) {
	xhci_configure_ep_ctx_t *configure_ctx = ctx;
	usb_completion_callback_t callback = configure_ctx->callback;
	void *callback_ctx = configure_ctx->callback_ctx;

	if (status != USB_STATUS_SUCCESS) {
		xhci_device_t *xhci_dev = container_of(dev, xhci_device_t, device);
		xhci_release_ep_ring(xhci_dev, configure_ctx->ep_index);
	}

	free(configure_ctx);

	if (callback)
		callback(dev, callback_ctx, status, transferred);
}

static int xhci_ctrl_configure_ep(usb_ctrl_t *ctrl, usb_device_t *dev, usb_endpoint_t *ep, usb_completion_callback_t callback, void *callback_ctx) {
	xhci_ctrl_t *xhci = container_of(ctrl, xhci_ctrl_t, ctrl);
	xhci_device_t *xhci_dev = container_of(dev, xhci_device_t, device);

	bool is_in = (ep->desc->bEndpointAddress & USB_ENDPOINT_ADDRESS_DIR_IN) != 0;

	uint32_t ep_num = ep->desc->bEndpointAddress & USB_ENDPOINT_ADDRESS_NUM_MASK;
	uint32_t ep_index = ep_num ? ((ep_num << 1) | (is_in ? 1 : 0)) : 0;

	if (ep_index == 0 || ep_index > 31)
		return EINVAL;

	if (xhci_dev->ep_rings[ep_index - 1].ring != NULL) {
		printf("xhci: duplicate endpoint address 0x%02x\n", ep->desc->bEndpointAddress);
		return EINVAL;
	}

	xhci_configure_ep_ctx_t *configure_ctx = alloc(sizeof(xhci_configure_ep_ctx_t));
	if (configure_ctx == NULL)
		return ENOMEM;

	if (xhci_alloc_ring(xhci, &xhci_dev->ep_rings[ep_index - 1], false)) {
		free(configure_ctx);
		return ENOMEM;
	}

	xhci_update_input_context(xhci, xhci_dev);

	volatile xhci_input_ctx_t *input_ctx = xhci_get_input_ctrl_ctx(xhci, xhci_dev);
	volatile xhci_slot_ctx_t *slot_ctx = xhci_get_input_slot_ctx(xhci, xhci_dev);
	volatile xhci_ep_ctx_t *ep_ctx = xhci_get_input_ep_ctx(xhci, xhci_dev, ep_index - 1);

	memset((void *)ep_ctx, 0, sizeof(xhci_ep_ctx_t));

	input_ctx->a |= 1 << 0;
	input_ctx->a |= 1 << ep_index;

	if (slot_ctx->dw0.ctx_entries < ep_index)
		slot_ctx->dw0.ctx_entries = ep_index;

	int error = xhci_update_hub_slot_context(dev, slot_ctx);
	if (error) {
		free(configure_ctx);
		xhci_release_ep_ring(xhci_dev, ep_index);
		return error;
	}

	// Set up the endpoint context.
	uint8_t ep_type = ep->desc->bmAttributes & USB_ENDPOINT_ATTRIB_TYPE_MASK;
	uint16_t max_packet_size = usb_endpoint_max_packet_size(ep->desc);
	uint8_t max_burst_size = 0;
	uint16_t max_esit_payload = max_packet_size;

	if ((dev->speed == USB_SPEED_SUPER || dev->speed == USB_SPEED_SUPER_PLUS) && ep->ss_companion == NULL) {
		free(configure_ctx);
		xhci_release_ep_ring(xhci_dev, ep_index);
		return EINVAL;
	}

	if (ep->ss_companion != NULL) {
		max_burst_size = ep->ss_companion->bMaxBurst;
		max_esit_payload = ep->ss_companion->wBytesPerInterval;
	} else if (dev->speed == USB_SPEED_HIGH) {
		uint8_t transactions = ((ep->desc->wMaxPacketSize >> 11) & 0x3) + 1;
		max_esit_payload = max_packet_size * transactions;
	}

	if (ep_type == USB_ENDPOINT_ATTRIB_TYPE_BULK) {
		ep_ctx->dw1.ep_type = is_in ? XHCI_EP_TYPE_BULK_IN : XHCI_EP_TYPE_BULK_OUT;
	} else if (ep_type == USB_ENDPOINT_ATTRIB_TYPE_INTR) {
		if (ep->desc->bInterval == 0) {
			free(configure_ctx);
			xhci_release_ep_ring(xhci_dev, ep_index);
			return EINVAL;
		}

		if (dev->speed == USB_SPEED_LOW || dev->speed == USB_SPEED_FULL)
			ep_ctx->dw0.interval = ceil_log2(ep->desc->bInterval * 8);
		else
			ep_ctx->dw0.interval = ep->desc->bInterval - 1;

		ep_ctx->dw1.ep_type = is_in ? XHCI_EP_TYPE_INTR_IN : XHCI_EP_TYPE_INTR_OUT;
		ep_ctx->dw4.max_esit_payload_lo = max_esit_payload;
	} else {
		free(configure_ctx);
		xhci_release_ep_ring(xhci_dev, ep_index);
		return EINVAL;
	}

	ep_ctx->dw1.cerr = 3;
	ep_ctx->dw1.max_packet_size = max_packet_size;
	ep_ctx->dw1.max_burst_size = max_burst_size;

	// Set up the ring dequeue pointer.
	uint64_t ep_ring_phys = (uint64_t)xhci_dev->ep_rings[ep_index - 1].ring_phys;
	ep_ctx->dw2.dcs = xhci_dev->ep_rings[ep_index - 1].cycle;
	ep_ctx->dw2.tr_dequeue_pointer_lo = (uint32_t)((ep_ring_phys >> 4) & 0xffffffff);
	ep_ctx->dw3.tr_dequeue_pointer_hi = (uint32_t)(ep_ring_phys >> 32);

	configure_ctx->callback = callback;
	configure_ctx->callback_ctx = callback_ctx;
	configure_ctx->ep_index = ep_index;

	xhci_submission_t xhci_submission = {
		.callback = configure_ep_callback,
		.callback_ctx = configure_ctx,
		.device = &xhci_dev->device,
	};

	xhci_trb_t trb = {
		.parameters = (uint64_t)xhci_dev->input_ctx_phys,
		.dw3 = XHCI_TRB_DW3_TYPE(TRB_CONFIGURE_EP) | (xhci_dev->slot_id << 24)
	};

	xhci_ring_submit(&xhci->command_ring, &trb, &xhci_submission);
	xhci_ring_command_doorbell(xhci);
	return 0;
}

static int xhci_ctrl_xfer(usb_ctrl_t *ctrl, usb_device_t *dev, usb_xfer_t *xfer) {
	xhci_ctrl_t *xhci = container_of(ctrl, xhci_ctrl_t, ctrl);
	xhci_device_t *xhci_dev = container_of(dev, xhci_device_t, device);

	int error = 0;
	xhci_submission_t sub = {
		.callback = xfer->completion,
		.callback_ctx = xfer->completion_ctx,
		.device = xfer->device,
	};

	if (xfer->type == USB_XFER_TYPE_CONTROL)
		error = xhci_control_xfer(xhci, xhci_dev, xfer, &sub);
	else
		error = xhci_data_xfer(xhci, xhci_dev, xfer, &sub);

	return error;
}

static void deaddress_device_callback(usb_device_t *device, void *, usb_status_t status, size_t) {
	xhci_device_t *xhci_dev = container_of(device, xhci_device_t, device);
	xhci_ctrl_t *xhci = (xhci_ctrl_t *)xhci_dev->device.hub->ctrl;

	if (status != USB_STATUS_SUCCESS) {
		printf("xhci: disable slot %u failed during deaddress\n", xhci_dev->slot_id);
		// If Disable Slot fails, the controller may still reference this device's
		// contexts or transfer rings. Leak them for now instead of freeing memory
		// that hardware may still own.
	} else {
		xhci_release_device_resources(xhci, xhci_dev);
	}
}

static void xhci_deaddress_device(usb_ctrl_t *ctrl, usb_device_t *device) {
	xhci_ctrl_t *xhci = container_of(ctrl, xhci_ctrl_t, ctrl);
	xhci_device_t *xhci_dev = container_of(device, xhci_device_t, device);

	xhci_disable_slot(xhci, xhci_dev, deaddress_device_callback, NULL);
}

usb_ctrl_ops_t xhci_ops = {
	.address_device = xhci_ctrl_address_device,
	.configure_ep = xhci_ctrl_configure_ep,
	.xfer = xhci_ctrl_xfer,
	.deaddress_device = xhci_deaddress_device
};
