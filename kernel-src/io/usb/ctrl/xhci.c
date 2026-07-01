#include <arch/cpu.h>
#include <errno.h>
#include <kernel/alloc.h>
#include <kernel/init.h>
#include <kernel/interrupt.h>
#include <kernel/pci.h>
#include <kernel/page.h>
#include <kernel/usb.h>
#include <kernel/mm.h>
#include <list.h>
#include <logging.h>
#include <util.h>
#include <kernel/xhci.h>

static uint32_t xhci_trb_type(xhci_trb_t *trb) {
	return (trb->dw3 >> 10) & 0x3f;
}

static void xhci_signal_event_thread(xhci_ctrl_t *xhci) {
	semaphore_signal_limit(&xhci->event_sem, 1);
}

static void xhci_signal_callback_thread(xhci_ctrl_t *xhci) {
	semaphore_signal_limit(&xhci->callback_sem, 1);
}

static void xhci_release_submission_page(xhci_submission_t *submission) {
	uint32_t cmd_type = xhci_trb_type(&submission->cmd_trb);

	if ((cmd_type == TRB_NORMAL || cmd_type == TRB_DATA_STAGE) && submission->cmd_trb.parameters != 0)
		mm_release_page((void *)(submission->cmd_trb.parameters & ~(PAGE_SIZE - 1)));
}

static bool xhci_reclaim_submission_slot(xhci_submission_t *slot, xhci_submission_t *submission) {
	bool valid = __atomic_exchange_n(&slot->valid, false, __ATOMIC_SEQ_CST);
	__assert(valid);

	xhci_release_submission_page(submission);
	xhci_ring_unreserve(submission->ring, 1);
	return true;
}

static void xhci_reclaim_submission_chain(xhci_submission_t *submissionp) {
	while (submissionp != NULL) {
		if (!__atomic_load_n(&submissionp->valid, __ATOMIC_SEQ_CST))
			return;

		xhci_submission_t submission = *submissionp;
		xhci_submission_t *next = submission.next;
		xhci_reclaim_submission_slot(submissionp, &submission);
		submissionp = next;
	}
}

static void xhci_enqueue_completion(xhci_ctrl_t *xhci, xhci_completion_t *completion) {
	semaphore_wait(&xhci->callback_space_sem, false);

	MUTEX_ACQUIRE(&xhci->callback_mutex);
	size_t written = ringbuffer_write(&xhci->callback_ring, completion, sizeof(*completion));
	__assert(written == sizeof(*completion));
	MUTEX_RELEASE(&xhci->callback_mutex);

	xhci_signal_callback_thread(xhci);
}

static void handle_submission_complete(xhci_ctrl_t *xhci, xhci_submission_t *submissionp) {
	xhci_submission_t submission = *submissionp;

	uint32_t cmd_type = xhci_trb_type(&submission.cmd_trb);
	if (cmd_type == TRB_SETUP_STAGE) {
		if (submission.usb_status == USB_STATUS_SUCCESS)
			return;
	} else if (cmd_type == TRB_NORMAL || cmd_type == TRB_DATA_STAGE) {
		// Check if we need to wait for more TRBs to complete.
		if (submission.usb_status == USB_STATUS_SUCCESS && (cmd_type == TRB_DATA_STAGE || (submission.cmd_trb.dw3 & XHCI_TRB_DW3_CH) != 0))
			return;
	}

	// Complete the transfer.
	size_t xfer_len;
	uint32_t status = (submission.event_trb.dw2 >> 24) & 0xff;
	if (status == TRB_SUCCESS) {
		xfer_len = submission.data_offset + submission.data_length;
	} else if (status == TRB_SHORT_PACKET) {
		size_t remainder = submission.event_trb.dw2 & 0xffffff;
		size_t trb_xfer_len = submission.data_length - min(remainder, submission.data_length);
		xfer_len = submission.data_offset + trb_xfer_len;
	} else {
		xfer_len = 0;
	}

	if (cmd_type == TRB_DATA_STAGE && submission.usb_status == USB_STATUS_SHORT_PACKET) {
		// surgically edit the status stage trb to return a short packet status with the final length
		__assert(submission.next != NULL);
		submission.next->usb_status = USB_STATUS_SHORT_PACKET;
		submission.next->data_offset = xfer_len;
		return;
	}

	// for ENABLE_SLOT commands, we use xfer_len for the slot id
	if ((submission.cmd_trb.dw3 & XHCI_TRB_DW3_TYPE_MASK) == XHCI_TRB_DW3_TYPE(TRB_ENABLE_SLOT))
		xfer_len = (submission.event_trb.dw3 >> 24) & 0xff;

	xhci_submission_t *td_head = submission.td_head != NULL ? submission.td_head : submissionp;
	xhci_reclaim_submission_chain(td_head);

	if (submission.callback) {
		xhci_completion_t completion = {
			.device = submission.device,
			.callback = submission.callback,
			.callback_ctx = submission.callback_ctx,
			.status = submission.usb_status,
			.transferred = xfer_len,
		};

		xhci_enqueue_completion(xhci, &completion);
	}
}

static xhci_submission_t *xhci_pop_completed_submission(xhci_ctrl_t *xhci) {
	long ipl = spinlock_acquire_raise_ipl(&xhci->event_lock, IPL_USB);
	xhci_submission_t *submission = (xhci_submission_t *)list_pop_front(&xhci->completion_list);
	spinlock_release_lower_ipl(&xhci->event_lock, ipl);
	return submission;
}

static int xhci_pop_port_change(xhci_ctrl_t *xhci) {
	long ipl = spinlock_acquire_raise_ipl(&xhci->event_lock, IPL_USB);

	int port = bitmap_find_first_set(&xhci->port_change_bitmap);
	if (port != -1)
		bitmap_set(&xhci->port_change_bitmap, port, 0);

	spinlock_release_lower_ipl(&xhci->event_lock, ipl);
	return port;
}

static bool xhci_pop_completion(xhci_ctrl_t *xhci, xhci_completion_t *completion) {
	bool ret = false;

	MUTEX_ACQUIRE(&xhci->callback_mutex);
	if (RINGBUFFER_DATACOUNT(&xhci->callback_ring) >= sizeof(*completion)) {
		size_t read = ringbuffer_read(&xhci->callback_ring, completion, sizeof(*completion));
		__assert(read == sizeof(*completion));
		ret = true;
	}
	MUTEX_RELEASE(&xhci->callback_mutex);

	if (ret)
		semaphore_signal(&xhci->callback_space_sem);

	return ret;
}

static void xhci_event_thread(void) {
	xhci_ctrl_t *xhci = current_thread()->kernelarg;

	for (;;) {
		semaphore_wait(&xhci->event_sem, false);

		for (;;) {
			xhci_submission_t *submission = xhci_pop_completed_submission(xhci);
			if (submission == NULL)
				break;

			handle_submission_complete(xhci, submission);
		}
	}
}

static void xhci_callback_thread(void) {
	xhci_ctrl_t *xhci = current_thread()->kernelarg;

	for (;;) {
		semaphore_wait(&xhci->callback_sem, false);

		for (;;) {
			bool did_work = false;

			for (;;) {
				int port = xhci_pop_port_change(xhci);
				if (port == -1)
					break;

				xhci_handle_port_change_event(xhci, port + 1);
				did_work = true;
			}

			for (;;) {
				xhci_completion_t completion;
				if (!xhci_pop_completion(xhci, &completion))
					break;

				completion.callback(completion.device, completion.callback_ctx, completion.status, completion.transferred);
				did_work = true;
			}

			if (!did_work)
				break;
		}
	}
}


static void xhci_dpc(context_t *, dpcarg_t dpcarg) {
	xhci_ctrl_t *xhci = dpcarg;
	volatile xhci_ir_t *ir = &xhci->rtregs->ir[0];

	// Process events from the event ring.
	bool signal_event_thread = false;
	bool signal_callback_thread = false;
	xhci_trb_t *r_trb;
	while ((r_trb = xhci_ring_dequeue(&xhci->event_ring)) != NULL) {
		uint32_t type = (r_trb->dw3 >> 10) & 0x3f;
		if (type == TRB_COMMAND_COMPLETION_EVENT || type == TRB_XFER_COMPLETION_EVENT) {
			uint32_t status = (r_trb->dw2 >> 24) & 0xff;

			usb_status_t usb_status;
			switch (status) {
				case TRB_SUCCESS:
					usb_status = USB_STATUS_SUCCESS;
					break;
				case TRB_SHORT_PACKET:
					usb_status = USB_STATUS_SHORT_PACKET;
					break;
				case TRB_DATA_BUFFER_ERROR:
				case TRB_BABBLE_DETECTED:
				case TRB_TRANSACTION_ERROR:
				case TRB_RESOURCE_ERROR:
				case TRB_BANDWIDTH_ERROR:
				case TRB_NO_SLOTS_AVAILABLE:
				case TRB_INVALID_STREAM_TYPE:
				case TRB_SLOT_NOT_ENABLED:
				case TRB_ENDPOINT_NOT_ENABLED:
				case TRB_BANDWIDTH_OVERRUN_ERROR:
				case TRB_CONTEXT_STATE_ERROR:
				case TRB_NO_PING_RESPONSE:
				case TRB_INCOMPATIBLE_DEVICE_ERROR:
				case TRB_COMMAND_RING_STOPPED:
				case TRB_COMMAND_RING_ABORTED:
				case TRB_STOPPED:
				case TRB_STOPPED_LEN:
				case TRB_STOPPED_SHORT:
				case TRB_LATENCY_TOO_LARGE:
				case TRB_ERROR_RESERVED:
				case TRB_ERROR_UNDEFINED:
				case TRB_INVALID_STREAM_ID:
				case TRB_SECONDARY_BANDWIDTH_ERROR:
				case TRB_SPLIT_TRANSACTION_ERROR:
					usb_status = USB_STATUS_ERROR;
					break;
				case TRB_STALL:
					usb_status = USB_STATUS_STALL;
					break;
				case TRB_BUFFER_OVERRUN:
				case TRB_RING_UNDERRUN:
				case TRB_RING_OVERRUN:
				case TRB_MISSED_SERVICE:
					usb_status = USB_STATUS_FLOW_ERROR;
					break;
				default:
					printf("xhci: bad event: %lu (driver bug!)\n", status);
					_panic("Driver bug", NULL);
					break;
			}

			xhci_ring_t *ring;
			if (type == TRB_COMMAND_COMPLETION_EVENT) {
				ring = &xhci->command_ring;
			} else {
				uint32_t slot_id = (r_trb->dw3 >> 24) & 0xff;
				__assert(slot_id > 0 && slot_id <= xhci->slot_count);
				xhci_device_t *dev = xhci->slots[slot_id - 1];
				__assert(dev);

				uint8_t ep_index = (r_trb->dw3 >> 16) & 0x1f;
				__assert(ep_index > 0);
				ring = &dev->ep_rings[ep_index - 1];
			}

			xhci_trb_t *trb = MAKE_HHDM(r_trb->parameters);
			uintptr_t trb_addr = (uintptr_t)trb;
			uintptr_t ring_start = (uintptr_t)ring->ring;
			uintptr_t ring_end = (uintptr_t)&ring->ring[ring->size - 1];
			__assert(trb_addr >= ring_start && trb_addr < ring_end);
			__assert((trb_addr - ring_start) % sizeof(xhci_trb_t) == 0);
			size_t trb_index = (trb_addr - ring_start) / sizeof(xhci_trb_t);
			xhci_submission_t *sub = &ring->submissions[trb_index];
			__assert(__atomic_load_n(&sub->valid, __ATOMIC_SEQ_CST));

			memcpy(&sub->event_trb, r_trb, sizeof(xhci_trb_t));

			if (sub->usb_status == USB_STATUS_SHORT_PACKET) {
				// this is a status stage trb and the data stage returned a short packet.
				// the offset has been surgically edited to the shorter length as to
				// return the proper transferred length to the callback.
				uint32_t original_cmd = xhci_trb_type(&sub->cmd_trb);
				__assert(original_cmd == TRB_STATUS_STAGE);
			} else {
				sub->usb_status = usb_status;
			}

			spinlock_acquire(&xhci->event_lock);
			list_push_back(&xhci->completion_list, &sub->list_node);
			spinlock_release(&xhci->event_lock);

			signal_event_thread = true;
		} else if (type == TRB_PORT_STATUS_CHANGE_EVENT) {
			uint8_t port_id = (r_trb->dw0 >> 24) & 0xff;

			spinlock_acquire(&xhci->event_lock);
			bitmap_set(&xhci->port_change_bitmap, port_id - 1, 1);
			spinlock_release(&xhci->event_lock);
			signal_callback_thread = true;
		}

		// Update the Event Ring Dequeue Pointer.
		uint64_t event_ring_phys = (uint64_t)xhci->event_ring.ring_phys;
		event_ring_phys += xhci->event_ring.index * sizeof(xhci_trb_t);
		ir->erdp = event_ring_phys | XHCI_ERDP_EHB;
	}

	if (signal_event_thread)
		xhci_signal_event_thread(xhci);
	if (signal_callback_thread)
		xhci_signal_callback_thread(xhci);
}

static void xhci_isr(isr_t *isr, context_t *ctx) {
	xhci_ctrl_t *xhci = isr->priv;

	// Check if there is an interrupt pending.
	volatile xhci_ir_t *ir = &xhci->rtregs->ir[0];
	uint32_t iman = ir->iman;
	uint32_t usbsts = xhci->opregs->usbsts;
	uint64_t erdp = ir->erdp;

	if ((erdp & XHCI_ERDP_EHB) == 0 && (iman & XHCI_IMAN_IP) == 0 && (usbsts & XHCI_USBSTS_EINT) == 0)
		return;

	// Clear the interrupt pending bit.
	if (iman & XHCI_IMAN_IP)
		ir->iman |= XHCI_IMAN_IP;
	if (usbsts & XHCI_USBSTS_EINT)
		xhci->opregs->usbsts = XHCI_USBSTS_EINT;

	dpc_enqueue(&xhci->dpc, xhci);
}

static int xhci_halt(xhci_ctrl_t *ctrl) {
	if ((ctrl->opregs->usbcmd & XHCI_USBCMD_RS) == 0)
		return 0;

	ctrl->opregs->usbcmd &= ~XHCI_USBCMD_RS;

	timespec_t start = timekeeper_timefromboot();
	while ((ctrl->opregs->usbsts & XHCI_USBSTS_HCH) == 0) {
		sched_sleep_us(10 * 1000); // 10ms

		timespec_t now = timekeeper_timefromboot();
		if (timespec_diffms(now, start) > 1000)
			return -1;
	}

	return 0;
}

static int xhci_reset(xhci_ctrl_t *ctrl) {
	ctrl->opregs->usbcmd |= XHCI_USBCMD_HCRST;

	timespec_t start = timekeeper_timefromboot();
	while ((ctrl->opregs->usbcmd & XHCI_USBCMD_HCRST) != 0 || (ctrl->opregs->usbsts & XHCI_USBSTS_CNR) != 0) {
		sched_sleep_us(10 * 1000); // 10ms

		timespec_t now = timekeeper_timefromboot();
		if (timespec_diffms(now, start) > 1000)
			return -1;
	}

	return 0;
}

static int xhci_run(xhci_ctrl_t *ctrl) {
	if ((ctrl->opregs->usbcmd & XHCI_USBCMD_RS))
		return 0;

	ctrl->opregs->usbcmd |= XHCI_USBCMD_RS | XHCI_USBCMD_INTE;

	timespec_t start = timekeeper_timefromboot();
	while ((ctrl->opregs->usbsts & XHCI_USBSTS_HCH) != 0 || (ctrl->opregs->usbsts & XHCI_USBSTS_CNR) != 0) {
		sched_sleep_us(10 * 1000); // 10ms

		timespec_t now = timekeeper_timefromboot();
		if (timespec_diffms(now, start) > 1000)
			return -1;
	}

	return 0;
}

static volatile uint32_t *xhci_ext_caps(pcibar_t bar0) {
	volatile xhci_caps_t *caps = (xhci_caps_t *)bar0.address;
	uint32_t xecp = (caps->hccparams1 >> 16) & 0xffff;
	if (xecp == 0)
		return NULL;
	return (uint32_t *)(bar0.address + (xecp << 2));
}

static int xhci_handoff(pcibar_t bar0) {
	volatile uint32_t *ext_caps = xhci_ext_caps(bar0);
	if (ext_caps == NULL)
		return 0;

	for (;;) {
		uint8_t cap_id = *ext_caps & 0xff;
		uint8_t next_cap_off = (*ext_caps >> 8) & 0xff;

		if (cap_id == 1) {
			printf("xhci: found BIOS/OS ownership capability\n");

			bool is_bios_owned = (*ext_caps >> 16) & 0x1;
			if (is_bios_owned) {
				printf("xhci: attempting to take ownership from BIOS\n");

				// Set OS Owned Semaphore.
				*ext_caps |= (1 << 24);

				timespec_t start = timekeeper_timefromboot();
				while (is_bios_owned) {
					sched_sleep_us(500000); // 500ms
					is_bios_owned = (*ext_caps >> 16) & 0x1;
					timespec_t now = timekeeper_timefromboot();
					if (timespec_diffms(now, start) > 5000) {
						printf("xhci: BIOS ownership handoff timeout\n");
						return ETIMEDOUT;
					}
				}

				printf("xhci: took ownership of controller from BIOS\n");
			}
		}

		if (next_cap_off == 0)
			break;
		ext_caps += next_cap_off;
	}

	return 0;
}

#define XHCI_INTEL_VENDOR_ID 0x8086
#define XHCI_INTEL_XUSB2PR 0xd0
#define XHCI_INTEL_USB2PRM 0xd4
#define XHCI_INTEL_USB3_PSSEN 0xd8
#define XHCI_INTEL_USB3PRM 0xdc

static bool xhci_intel_has_ehci_companion(void) {
	return pci_getenum(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_SERIAL_BUS_USB, PCI_PROGIF_USB_EHCI, XHCI_INTEL_VENDOR_ID, -1, -1, 0) != NULL;
}

static void xhci_intel_route_ports_to_xhci(pcienum_t *e) {
	if (e->vendor != XHCI_INTEL_VENDOR_ID)
		return;

	if (!xhci_intel_has_ehci_companion())
		return;

	uint32_t usb2_mask = PCI_READ32(e, XHCI_INTEL_USB2PRM);
	uint32_t usb3_mask = PCI_READ32(e, XHCI_INTEL_USB3PRM);
	uint32_t usb2_before = PCI_READ32(e, XHCI_INTEL_XUSB2PR);
	uint32_t usb3_before = PCI_READ32(e, XHCI_INTEL_USB3_PSSEN);

	if (usb3_mask != 0 && usb3_mask != UINT32_MAX)
		PCI_WRITE32(e, XHCI_INTEL_USB3_PSSEN, usb3_mask);
	if (usb2_mask != 0 && usb2_mask != UINT32_MAX)
		PCI_WRITE32(e, XHCI_INTEL_XUSB2PR, usb2_mask);

	uint32_t usb2_after = PCI_READ32(e, XHCI_INTEL_XUSB2PR);
	uint32_t usb3_after = PCI_READ32(e, XHCI_INTEL_USB3_PSSEN);

	printf("xhci: Intel routed ports to xHCI: usb2 %08x->%08x usb3 %08x->%08x\n", usb2_before, usb2_after, usb3_before, usb3_after);
}


static int xhci_ctrl_start(usb_ctrl_t *ctrl) {
	int ret = 0;
	xhci_ctrl_t *xhci = container_of(ctrl, xhci_ctrl_t, ctrl);
	pcibar_t bar0 = pci_getbar(xhci->pci_enum, 0);

	ret = xhci_halt(xhci);
	if (ret != 0) {
		printf("xhci: controller halt timeout\n");
		return ret;
	}

	ret = xhci_reset(xhci);
	if (ret != 0) {
		printf("xhci: controller reset timeout\n");
		return ret;
	}

	// Enable MSI interrupts.
	size_t intcount;
	if (xhci->pci_enum->msix.exists) {
		intcount = pci_initmsix(xhci->pci_enum);
	} else if (xhci->pci_enum->msi.exists) {
		intcount = pci_initmsi(xhci->pci_enum, 1);
	} else {
		printf("xhci: controller does not support MSI or MSI-X\n");
		return ENODEV;
	}

	__assert(intcount > 0);

	isr_t *isr = interrupt_allocate(xhci_isr, ARCH_EOI, IPL_USB);
	__assert(isr != NULL);
	isr->priv = ctrl;

	if (xhci->pci_enum->msix.exists) {
		pci_msixadd(xhci->pci_enum, 0, INTERRUPT_IDTOVECTOR(isr->id), 1, 0);
		pci_msixsetmask(xhci->pci_enum, 0);
	} else {
		pci_msisetbase(xhci->pci_enum, INTERRUPT_IDTOVECTOR(isr->id), 1, 0);
		pci_msisetmask(xhci->pci_enum, 0, 0);
	}

	uint32_t max_slots = xhci->caps->hcsparams1 & 0xff;
	uint32_t max_ports = (xhci->caps->hcsparams1 >> 24) & 0xff;

	printf("xhci: controller supports %u slots and %u ports\n", max_slots, max_ports);

	xhci->port_count = max_ports;
	xhci->ports = alloc(sizeof(xhci_port_protocol_t) * max_ports);
	__assert(xhci->ports);
	__assert(bitmap_init(&xhci->port_change_bitmap, max_ports) == 0);

	for (uint32_t i = 0; i < max_ports; i++) {
		xhci->ports[i].ver_major = 0;
		xhci->ports[i].ver_minor = 0;
	}

	xhci->slot_count = max_slots;
	xhci->slots = alloc(sizeof(xhci_device_t *) * max_slots);
	__assert(xhci->slots);

	xhci->ctx_stride = (xhci->caps->hccparams1 & XHCI_HCCPARAMS1_CSZ) != 0 ? 64 : 32;

	volatile uint32_t *ext_caps = xhci_ext_caps(bar0);
	if (ext_caps == NULL) {
		printf("xhci: controller has no extended capabilities\n");
		return ENODEV;
	}

	for (;;) {
		uint8_t cap_id = *ext_caps & 0xff;
		uint8_t next_cap_off = (*ext_caps >> 8) & 0xff;

		if (cap_id == 2) {
			// map ports to usb types
			uint8_t ver_major = (*ext_caps >> 24) & 0xff;
			uint8_t ver_minor = (*ext_caps >> 16) & 0xff;

			uint32_t raw_port_start = ext_caps[2] & 0xff;
			uint32_t port_count = (ext_caps[2] >> 8) & 0xff;

			if (raw_port_start == 0 || port_count == 0 || raw_port_start - 1 >= max_ports || port_count > max_ports - (raw_port_start - 1)) {
				printf("xhci: invalid usb %u.%u port range start=%u count=%u max=%u\n",
					ver_major,
					ver_minor,
					raw_port_start,
					port_count,
					max_ports);
				return EINVAL;
			}

			uint32_t port_start = raw_port_start - 1;

			printf("xhci: %u usb %u.%u ports starting at port %u\n",
				port_count, ver_major, ver_minor, port_start + 1);

			for (uint32_t i = 0; i < port_count; i++) {
				xhci->ports[port_start + i].ver_major = ver_major;
				xhci->ports[port_start + i].ver_minor = ver_minor;
			}

			xhci_root_hub_t *hub = alloc(sizeof(xhci_root_hub_t));
			__assert(hub);

			snprintf(hub->hub.name, sizeof(hub->hub.name), "xhci-root-hub-%u.%u", ver_major, ver_minor);

			hub->hub.ctrl = ctrl;
			hub->hub.ops = &xhci_root_hub_ops;
			hub->hub.device = NULL;

			hub->hub.ports = alloc(sizeof(usb_hub_port_t) * port_count);
			__assert(hub->hub.ports);
			hub->hub.port_count = port_count;

			hub->port_offset = port_start;

			list_push_back(&xhci->hubs, &hub->list_node);
		}

		if (next_cap_off == 0)
			break;

		ext_caps += next_cap_off;
	}

	__assert(xhci_alloc_ring(xhci, &xhci->command_ring, false) == 0);
	__assert(xhci_alloc_ring(xhci, &xhci->event_ring, true) == 0);

	// Set up MaxSlotsEn field.
	xhci->opregs->config = max_slots;

	// Set up the Device Context Base Address Array.
	void *dcbaa_phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
	__assert(dcbaa_phys);

	uint64_t *dcbaa_virt = MAKE_HHDM(dcbaa_phys);
	memset(dcbaa_virt, 0, PAGE_SIZE);

	// Set up scratchpad buffers if requested.
	uint32_t max_scratchpads_hi = (xhci->caps->hcsparams2 >> 21) & 0x1f;
	uint32_t max_scratchpads_lo = (xhci->caps->hcsparams2 >> 27) & 0x1f;
	uint32_t max_scratchpads = max_scratchpads_lo | (max_scratchpads_hi << 5);

	if (max_scratchpads > 0) {
		printf("xhci: setting up %u scratchpad buffers\n", max_scratchpads);
		__assert(max_scratchpads < PAGE_SIZE / sizeof(uint64_t));

		void *scratchpad_array_phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
		__assert(scratchpad_array_phys);
		uint64_t *scratchpad_array_virt = MAKE_HHDM(scratchpad_array_phys);

		for (uint32_t i = 0; i < max_scratchpads; i++) {
			void *scratchpad_phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
			__assert(scratchpad_phys);
			memset(MAKE_HHDM(scratchpad_phys), 0, PAGE_SIZE);
			scratchpad_array_virt[i] = (uint64_t)scratchpad_phys;
		}

		dcbaa_virt[0] = (uint64_t)scratchpad_array_phys;
	}

	xhci->dcbaa = dcbaa_virt;
	xhci->opregs->dcbaap = (uint64_t)dcbaa_phys;

	// Set up command rings.
	xhci->opregs->crcr = (uint64_t)xhci->command_ring.ring_phys | XHCI_CRCR_RCS;

	// Set up Event Ring Segment Table.
	void *erst_phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
	__assert(erst_phys);

	xhci_erst_entry_t *erst_virt = MAKE_HHDM(erst_phys);
	memset(erst_virt, 0, PAGE_SIZE);

	erst_virt[0].ring_segment = (uint64_t)xhci->event_ring.ring_phys;
	erst_virt[0].ring_segment_size = xhci->event_ring.size;

	// Set up the interrupter.
	volatile xhci_ir_t *ir = &xhci->rtregs->ir[0];
	ir->iman = XHCI_IMAN_IE;
	ir->erstsz = 1;
	ir->erstba = (uint64_t)erst_phys;
	ir->erdp = (uint64_t)xhci->event_ring.ring_phys | XHCI_ERDP_EHB;

	// Start the controller.
	ret = xhci_run(xhci);
	if (ret != 0) {
		printf("xhci: controller run timeout\n");
		return ret;
	}

	// power on all ports
	bool debounce_wait = false;
	list_for_each (&xhci->hubs, node) {
		xhci_root_hub_t *rh = container_of(node, xhci_root_hub_t, list_node);
		usb_hub_t *hub = &rh->hub;

		for (uint8_t port = 0; port < hub->port_count; port++) {
			volatile xhci_port_regs_t *portregs = &xhci->portregs[rh->port_offset + port];
			uint32_t portsc = portregs->portsc;
			portregs->portsc = (portsc & XHCI_PORTSC_PRESERVE_BITS) | (portsc & XHCI_PORTSC_CHANGE_BITS);
			if ((portsc & XHCI_PORTSC_PP) == 0) {
				portregs->portsc = (portsc & XHCI_PORTSC_CHANGE_BITS) | XHCI_PORTSC_PP;
				debounce_wait = true;
			}
		}
	}

	if (debounce_wait)
		sched_sleep_us(100000); // 100 ms

	// reset all conected ports
	list_for_each (&xhci->hubs, node) {
		xhci_root_hub_t *rh = container_of(node, xhci_root_hub_t, list_node);
		usb_hub_t *hub = &rh->hub;

		for (uint8_t port = 0; port < hub->port_count; port++) {
			volatile xhci_port_regs_t *portregs = &xhci->portregs[rh->port_offset + port];
			// NOTE: while USB3 ports don't need this, we will still do it to simplify the path
			uint32_t portsc = portregs->portsc;
			if (portsc & XHCI_PORTSC_CCS)
				portregs->portsc = (portsc & XHCI_PORTSC_PRESERVE_BITS) | XHCI_PORTSC_PR;
		}
	}

	return 0;
}


static void init_ctrl(pcienum_t *e) {
	printf("xhci: found controller at %02x:%02x.%x\n", e->bus, e->device, e->function);

	pci_setcommand(e, PCI_COMMAND_MMIO, 1);

	// perform handoff before changing anything else
	pcibar_t bar0 = pci_getbar(e, 0);
	if (xhci_handoff(bar0) != 0)
		return;

	xhci_intel_route_ports_to_xhci(e);

	pci_setcommand(e, PCI_COMMAND_IO, 0);
	pci_setcommand(e, PCI_COMMAND_IRQDISABLE, 1);
	pci_setcommand(e, PCI_COMMAND_BUSMASTER, 1);

	volatile xhci_caps_t *caps = (xhci_caps_t *)bar0.address;
	volatile xhci_opregs_t *opregs = (xhci_opregs_t *)(bar0.address + caps->caplength);
	volatile xhci_rtregs_t *rtregs = (xhci_rtregs_t *)(bar0.address + caps->rtsoff);

	// Only 64-bit-addressing controllers are supported for simplicity.
	if ((caps->hccparams1 & XHCI_HCCPARAMS1_AC64) == 0) {
		printf("xhci: controller does not support 64-bit addressing\n");
		return;
	}

	// Make sure the controller supports 4K pages
	if ((opregs->pagesize & (1 << 0)) == 0) {
		printf("xhci: controller does not support 4K page size\n");
		return;
	}

	// Register the controller with the USB subsystem
	xhci_ctrl_t *ctrl = alloc(sizeof(xhci_ctrl_t));
	__assert(ctrl);

	ctrl->ctrl.ops = &xhci_ops;

	SEMAPHORE_INIT(&ctrl->event_sem, 0);
	SEMAPHORE_INIT(&ctrl->callback_sem, 0);
	SEMAPHORE_INIT(&ctrl->callback_space_sem, XHCI_CALLBACK_RING_ENTRIES);
	SPINLOCK_INIT(ctrl->event_lock);
	MUTEX_INIT(&ctrl->callback_mutex);
	list_init(&ctrl->completion_list);
	__assert(ringbuffer_init(&ctrl->callback_ring, sizeof(xhci_completion_t) * XHCI_CALLBACK_RING_ENTRIES) == 0);
	dpc_prepare(&ctrl->dpc, xhci_dpc);

	list_init(&ctrl->hubs);

	ctrl->pci_enum = e;
	ctrl->caps = caps;
	ctrl->opregs = opregs;
	ctrl->rtregs = rtregs;
	ctrl->portregs = (xhci_port_regs_t *)(bar0.address + caps->caplength + 0x400);
	ctrl->dbs = (uint32_t *)(bar0.address + caps->dboff);

	int res = xhci_ctrl_start(&ctrl->ctrl);
	if (res) {
		printf("xhci: controller init returned an error: %s\n", strerror(res));
		return;
	}

	struct thread_t *event_thread = sched_newthread(xhci_event_thread, PAGE_SIZE * 4, 0, NULL, NULL);
	__assert(event_thread != NULL);
	event_thread->kernelarg = ctrl;
	sched_queue(event_thread);

	struct thread_t *callback_thread = sched_newthread(xhci_callback_thread, PAGE_SIZE * 4, 0, NULL, NULL);
	__assert(callback_thread != NULL);
	callback_thread->kernelarg = ctrl;
	sched_queue(callback_thread);
}

void xhci_init(void) {
	for (int i = 0;; ++i) {
		pcienum_t *e = pci_getenum(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_SERIAL_BUS_USB, PCI_PROGIF_USB_XHCI, -1, -1, -1, i);
		if (e == NULL)
			break;
		init_ctrl(e);
	}
}

INIT_ROUTINE_DEFINE(xhci, INIT_ROUTINE_FLAGS_NONE, xhci_init, acpi);
