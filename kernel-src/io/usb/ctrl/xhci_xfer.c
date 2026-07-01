#include <kernel/xhci.h>
#include <errno.h>
#include <kernel/alloc.h>
#include <kernel/mm.h>
#include <kernel/page.h>
#include <logging.h>

typedef struct {
	xhci_trb_t trb;
	void *page;
	size_t data_length;
	size_t data_offset;
} sg_trb_t;

static int xhci_count_sg_trbs(iovec_iterator_t *iov, size_t total_size, size_t *trb_count) {
	iovec_iterator_t iter = *iov;
	size_t done = 0;
	size_t count = 0;

	while (done < total_size) {
		size_t page_offset, page_remaining;
		void *page;

		int err = iovec_iterator_next_page(&iter, &page_offset, &page_remaining, &page);
		if (err)
			return err;
		if (page == NULL)
			return EFAULT;

		mm_release_page(page);

		done += min(page_remaining, total_size - done);
		count++;
	}

	*trb_count = count;
	return 0;
}

int xhci_data_xfer(xhci_ctrl_t *xhci, xhci_device_t *dev, usb_xfer_t *xfer, xhci_submission_t *sub) {
	__assert(xfer->ep != NULL);
	__assert(xfer->setup == NULL);

	uint32_t ep_num = xfer->ep->desc->bEndpointAddress & USB_ENDPOINT_ADDRESS_NUM_MASK;
	uint32_t ep_index = (ep_num << 1) | (xfer->ep->desc->bEndpointAddress & USB_ENDPOINT_ADDRESS_DIR_IN ? 1 : 0);

	xhci_ring_t *ring = &dev->ep_rings[ep_index - 1];
	int error = 0;

	if (xfer->flags & USB_XFER_FLAG_IOVEC) {
		error = xhci_sg_data_xfer(xhci, ring, dev, xfer, sub);
	} else {
		if (xfer->data_length == 0) {
			if (sub->callback)
				sub->callback(sub->device, sub->callback_ctx, USB_STATUS_SUCCESS, 0);
			return 0;
		}

		xhci_ring_reserve(ring, 1);

		// Make sure the data transfer does not cross a page boundary.
		uint64_t page_offset = (uint64_t)xfer->data & (PAGE_SIZE - 1);
		__assert(page_offset + xfer->data_length <= PAGE_SIZE);

		xhci_trb_t trb = {0};
		if (xfer->flags & USB_XFER_FLAG_BUFFER_PHYSICAL) {
			mm_hold_page(xfer->data - page_offset);
			trb.parameters = (uint64_t)xfer->data;
		} else {
			// This should never be called with user pages. Doing so is a bug. For user-fronting code, use
			// the scatter-gather variant
			__assert(!IS_USER_ADDRESS(xfer->data));
			void *page = mm_get_physical_address(xfer->data - page_offset, MM_GET_PHYSICAL_ADDRESS_FLAGS_HOLD);
			__assert(page != NULL);
			trb.parameters = (uint64_t)page + page_offset;
		}

		trb.dw2 = (uint32_t)xfer->data_length;
		trb.dw3 = XHCI_TRB_DW3_TYPE(TRB_NORMAL) | XHCI_TRB_DW3_ISP | XHCI_TRB_DW3_IOC;

		sub->data_length = xfer->data_length;
		sub->data_offset = 0;

		MUTEX_ACQUIRE(&ring->lock);
		xhci_submission_t *current = xhci_ring_submit_locked(ring, &trb, sub);
		current->td_head = current;
		MUTEX_RELEASE(&ring->lock);
	}

	if (error)
		return error;

	// Ring the doorbell for the endpoint.
	xhci_ring_endpoint_doorbell(xhci, dev, ep_index);
	return error;
}

int xhci_sg_data_xfer(xhci_ctrl_t *xhci, xhci_ring_t *ring, xhci_device_t *dev, usb_xfer_t *xfer, xhci_submission_t *sub) {
	uint16_t max_packet_size = usb_endpoint_max_packet_size(xfer->ep->desc);
	uint32_t td_count = ROUND_UP(xfer->iov->total_size, max_packet_size) / max_packet_size;
	uint32_t done = 0;

	if (xfer->iov->total_size == 0) {
		if (sub->callback)
			sub->callback(sub->device, sub->callback_ctx, USB_STATUS_SUCCESS, 0);
		return 0;
	}

	size_t trb_count;
	int err = xhci_count_sg_trbs(xfer->iov, xfer->iov->total_size, &trb_count);
	if (err)
		return err;

	xhci_ring_reserve(ring, trb_count);

	sg_trb_t *prepared = alloc(sizeof(sg_trb_t) * trb_count);
	if (prepared == NULL) {
		xhci_ring_unreserve(ring, trb_count);
		return ENOMEM;
	}
	memset(prepared, 0, sizeof(sg_trb_t) * trb_count);

	iovec_iterator_t iter = *xfer->iov;

	size_t prepared_count = 0;
	while (done < xfer->iov->total_size) {
		size_t page_offset, page_remaining;
		void *page;

		err = iovec_iterator_next_page(&iter, &page_offset, &page_remaining, &page);
		if (err)
			goto fail;
		if (page == NULL) {
			err = EFAULT;
			goto fail;
		}

		size_t to_xfer = min(page_remaining, xfer->iov->total_size - done);

		uint8_t td_size = td_count - (done + to_xfer) / max_packet_size;
		if (done + to_xfer == xfer->iov->total_size)
			td_size = 0;
		else if (td_size > 31)
			td_size = 31;

		xhci_trb_t trb = {0};
		trb.parameters = (uint64_t)page + page_offset;
		trb.dw2 = XHCI_TRB_DW2_TR_LEN(to_xfer) | XHCI_TRB_DW2_TD_SIZE(td_size);
		trb.dw3 = XHCI_TRB_DW3_TYPE(TRB_NORMAL) | XHCI_TRB_DW3_ISP;

		if (to_xfer + done < xfer->iov->total_size) {
			// More TRBs to come in this transfer, set the CH bit.
			trb.dw3 |= XHCI_TRB_DW3_CH;
		} else {
			trb.dw3 |= XHCI_TRB_DW3_IOC;
		}

		prepared[prepared_count].trb = trb;
		prepared[prepared_count].page = page;
		prepared[prepared_count].data_length = to_xfer;
		prepared[prepared_count].data_offset = done;
		prepared_count++;
		done += to_xfer;
	}

	*xfer->iov = iter;

	MUTEX_ACQUIRE(&ring->lock);
	xhci_submission_t *prev = NULL;
	for (size_t i = 0; i < prepared_count; ++i) {
		sub->data_length = prepared[i].data_length;
		sub->data_offset = prepared[i].data_offset;
		xhci_submission_t *current = xhci_ring_submit_locked(ring, &prepared[i].trb, sub);
		if (prev == NULL) {
			current->td_head = current;
		} else {
			current->td_head = prev->td_head;
			prev->next = current;
		}

		prev = current;
	}
	MUTEX_RELEASE(&ring->lock);

	free(prepared);
	return 0;

	fail:
	for (size_t i = 0; i < prepared_count; ++i)
		mm_release_page(prepared[i].page);
	free(prepared);
	xhci_ring_unreserve(ring, trb_count);
	return err;
}

int xhci_control_xfer(xhci_ctrl_t *xhci, xhci_device_t *dev, usb_xfer_t *xfer, xhci_submission_t *sub) {
	__assert(xfer->ep == NULL);
	__assert(xfer->setup != NULL);

	// Why would anyone want to do scatter-gather control transfers???
	__assert(!(xfer->flags & USB_XFER_FLAG_IOVEC));

	// Make sure direction matches request type and length matches buffer length.
	usb_setup_t *setup = xfer->setup;

	{
		bool flags_to_host = (xfer->flags & USB_XFER_FLAG_TO_HOST) != 0;
		bool req_to_host = (setup->bmRequestType & USB_REQUEST_DIR_TO_HOST) != 0;
		__assert(xfer->data_length == setup->wLength);
		__assert(flags_to_host == req_to_host);
	}

	bool has_data_stage = xfer->data != NULL && xfer->data_length > 0;
	bool data_stage_in = has_data_stage && xfer->flags & USB_XFER_FLAG_TO_HOST;

	xhci_ring_t *ring = &dev->ep_rings[0];

	size_t trb_count = has_data_stage ? 3 : 2;
	xhci_ring_reserve(ring, trb_count);

	xhci_trb_t setup_trb = {0};
	xhci_trb_t data_trb = {0};
	xhci_trb_t status_trb = {0};

	uint32_t trt = 0;
	if (has_data_stage)
		// 0 = no data stage, 2 = data out, 3 = data in
		trt = data_stage_in ? 3 : 2;

	setup_trb.dw0 = (uint32_t)setup->bmRequestType | ((uint32_t)setup->bRequest << 8) | ((uint32_t)setup->wValue << 16);
	setup_trb.dw1 = (uint32_t)setup->wIndex | ((uint32_t)setup->wLength << 16);
	setup_trb.dw2 = 8; // Always 8 bytes for setup stage.
	setup_trb.dw3 = XHCI_TRB_DW3_TYPE(TRB_SETUP_STAGE) | XHCI_TRB_DW3_TRT(trt) | XHCI_TRB_DW3_IDT;

	if (has_data_stage) {
		// Make sure the data transfer does not cross a page boundary.
		uint64_t page_offset = (uint64_t)xfer->data & (PAGE_SIZE - 1);
		__assert(page_offset + xfer->data_length <= PAGE_SIZE);

		if (xfer->data_length) {
			if (xfer->flags & USB_XFER_FLAG_BUFFER_PHYSICAL) {
				mm_hold_page(xfer->data - page_offset);
				data_trb.parameters = (uint64_t)xfer->data;
			} else {
				void *page = mm_get_physical_address(xfer->data - page_offset, MM_GET_PHYSICAL_ADDRESS_FLAGS_HOLD);
				if (page == NULL) {
					xhci_ring_unreserve(ring, trb_count);
					return EFAULT;
				}
				data_trb.parameters = (uint64_t)page + page_offset;
			}
		}

		data_trb.dw2 = (uint32_t)xfer->data_length;
		data_trb.dw3 = XHCI_TRB_DW3_TYPE(TRB_DATA_STAGE) | XHCI_TRB_DW3_ISP | (data_stage_in ? XHCI_TRB_DW3_DIR : 0);
	}

	status_trb.dw3 = XHCI_TRB_DW3_TYPE(TRB_STATUS_STAGE) | XHCI_TRB_DW3_IOC | (data_stage_in ? 0 : XHCI_TRB_DW3_DIR);

	MUTEX_ACQUIRE(&ring->lock);

	sub->data_length = 0;
	sub->data_offset = 0;
	xhci_submission_t *prev = xhci_ring_submit_locked(ring, &setup_trb, sub);
	xhci_submission_t *head = prev;
	prev->td_head = head;

	if (has_data_stage) {
		sub->data_length = xfer->data_length;
		sub->data_offset = 0;
		xhci_submission_t *current = xhci_ring_submit_locked(ring, &data_trb, sub);
		current->td_head = head;
		prev->next = current;
		prev = current;
	}

	sub->data_length = 0;
	sub->data_offset = has_data_stage ? xfer->data_length : 0;
	xhci_submission_t *status = xhci_ring_submit_locked(ring, &status_trb, sub);
	status->td_head = head;
	prev->next = status;

	MUTEX_RELEASE(&ring->lock);

	// Ring the doorbell for the control endpoint.
	xhci_ring_endpoint_doorbell(xhci, dev, 1);
	return 0;
}
