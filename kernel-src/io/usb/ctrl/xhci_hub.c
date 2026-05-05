#include <kernel/xhci.h>
#include <logging.h>

void xhci_handle_port_change_event(xhci_ctrl_t *xhci, int global_port) {
	int local_port = -1;
	xhci_root_hub_t *hub = NULL;
	list_for_each(&xhci->hubs, i) {
		hub = container_of(i, xhci_root_hub_t, list_node);
		int port = global_port - 1 - hub->port_offset;
		if (port >= 0 && port < hub->hub.port_count) {
			local_port = port;
			break;
		}
	}

	// getting an event on a non-existant hub is either a hardware bug or a driver bug
	// either way, we probably don't want to continue
	__assert(local_port != -1);

	uint32_t portsc = xhci->portregs[global_port - 1].portsc;
	bool reset_event = false;
	bool connect_event = false;
	bool disconnect_event = false;

	if (portsc & XHCI_PORTSC_PRC)
		reset_event = true;

	if (portsc & XHCI_PORTSC_CSC) {
		connect_event = portsc & XHCI_PORTSC_CCS;
		disconnect_event = !connect_event;
	}

	if (disconnect_event)
		usb_hub_event_disconnect(&hub->hub, local_port);
	else if (connect_event) {
		if (hub->hub.ports[local_port].device != NULL)
			usb_hub_event_disconnect(&hub->hub, local_port);
		usb_hub_event_connect(&hub->hub, local_port);
	} else if (reset_event) {
		usb_hub_event_reset(&hub->hub, local_port);
	}

	xhci->portregs[global_port - 1].portsc = (portsc & XHCI_PORTSC_PRESERVE_BITS) |
		(portsc & XHCI_PORTSC_CHANGE_BITS);
}

static int xhci_root_hub_get_port_status(usb_hub_t *hub, uint8_t port, uint16_t *status, uint16_t *change) {
	xhci_root_hub_t *rh = container_of(hub, xhci_root_hub_t, hub);
	xhci_ctrl_t *ctrl = container_of(hub->ctrl, xhci_ctrl_t, ctrl);
	++port;

	if (port == 0 || port > hub->port_count)
		return EINVAL;

	uint32_t portsc = ctrl->portregs[rh->port_offset + port - 1].portsc;
	uint32_t port_speed = (portsc >> 10) & 0xf;

	*status = 0;
	*change = 0;

	if (port_speed == XHCI_PORT_SPEED_LOW)
		*status |= USB_HUB_PORT_STATUS_LOW_SPEED;
	else if (port_speed == XHCI_PORT_SPEED_HIGH)
		*status |= USB_HUB_PORT_STATUS_HIGH_SPEED;

	if (portsc & XHCI_PORTSC_CCS)
		*status |= USB_HUB_PORT_STATUS_PORT_CONNECTION;
	if (portsc & XHCI_PORTSC_PED)
		*status |= USB_HUB_PORT_STATUS_PORT_ENABLE;
	if (portsc & XHCI_PORTSC_OCA)
		*status |= USB_HUB_PORT_STATUS_PORT_OVER_CURRENT;
	if (portsc & XHCI_PORTSC_PR)
		*status |= USB_HUB_PORT_STATUS_PORT_RESET;
	if (portsc & XHCI_PORTSC_PP)
		*status |= USB_HUB_PORT_STATUS_PORT_POWER;

	if (portsc & XHCI_PORTSC_CSC)
		*change |= USB_HUB_PORT_CHANGE_PORT_CONNECTION;
	if (portsc & XHCI_PORTSC_PEC)
		*change |= USB_HUB_PORT_CHANGE_PORT_ENABLE;
	if (portsc & XHCI_PORTSC_OCC)
		*change |= USB_HUB_PORT_CHANGE_PORT_OVER_CURRENT;
	if (portsc & XHCI_PORTSC_PRC)
		*change |= USB_HUB_PORT_CHANGE_PORT_RESET;

	return 0;
}

static int xhci_root_hub_set_port_feature(usb_hub_t *hub, uint8_t port, uint16_t feature) {
	xhci_root_hub_t *rh = container_of(hub, xhci_root_hub_t, hub);
	xhci_ctrl_t *ctrl = container_of(hub->ctrl, xhci_ctrl_t, ctrl);
	++port;

	if (port == 0 || port > hub->port_count)
		return EINVAL;

	volatile xhci_port_regs_t *portregs = &ctrl->portregs[rh->port_offset + port - 1];

	uint32_t portsc = portregs->portsc;
	portsc &= ~(XHCI_PORTSC_CCS | XHCI_PORTSC_PED | XHCI_PORTSC_OCA | XHCI_PORTSC_PR);
	portsc &= ~XHCI_PORTSC_CHANGE_BITS;

	if (feature == USB_HUB_FEATURE_PORT_RESET)
		portsc |= XHCI_PORTSC_PR;
	else if (feature == USB_HUB_FEATURE_PORT_POWER)
		portsc |= XHCI_PORTSC_PP;
	else
		return EINVAL;

	portregs->portsc = portsc;
	return 0;
}

static int xhci_root_hub_clear_port_feature(usb_hub_t *hub, uint8_t port, uint16_t feature) {
	xhci_root_hub_t *rh = container_of(hub, xhci_root_hub_t, hub);
	xhci_ctrl_t *ctrl = container_of(hub->ctrl, xhci_ctrl_t, ctrl);
	++port;

	if (port == 0 || port > hub->port_count)
		return EINVAL;

	volatile xhci_port_regs_t *portregs = &ctrl->portregs[rh->port_offset + port - 1];

	uint32_t portsc = portregs->portsc;
	portsc &= ~(XHCI_PORTSC_CCS | XHCI_PORTSC_PED | XHCI_PORTSC_OCA | XHCI_PORTSC_PR);
	portsc &= ~XHCI_PORTSC_CHANGE_BITS;

	if (feature == USB_HUB_FEATURE_PORT_ENABLE)
		portsc &= ~XHCI_PORTSC_PED;
	else if (feature == USB_HUB_FEATURE_PORT_RESET)
		portsc &= ~XHCI_PORTSC_PR;
	else if (feature == USB_HUB_FEATURE_PORT_POWER)
		portsc &= ~XHCI_PORTSC_PP;
	else if (feature == USB_HUB_FEATURE_C_PORT_CONNECTION)
		portsc |= XHCI_PORTSC_CSC;
	else if (feature == USB_HUB_FEATURE_C_PORT_ENABLE)
		portsc |= XHCI_PORTSC_PEC;
	else if (feature == USB_HUB_FEATURE_C_PORT_OVER_CURRENT)
		portsc |= XHCI_PORTSC_OCC;
	else if (feature == USB_HUB_FEATURE_C_PORT_RESET)
		portsc |= XHCI_PORTSC_PRC;
	else
		return EINVAL;

	portregs->portsc = portsc;
	return 0;
}

usb_hub_ops_t xhci_root_hub_ops = {
	.get_port_status = xhci_root_hub_get_port_status,
	.set_port_feature = xhci_root_hub_set_port_feature,
	.clear_port_feature = xhci_root_hub_clear_port_feature,
};
