#include <kernel/xhci.h>
#include <kernel/alloc.h>
#include <kernel/page.h>
#include <errno.h>
#include <logging.h>

int xhci_alloc_ring(xhci_ctrl_t *ctrl, xhci_ring_t *r, bool event_ring) {
	void *ring_phys = mm_alloc_page(MEMORY_SECTION_DEFAULT);
	if (ring_phys == NULL)
		return ENOMEM;

	xhci_trb_t *ring = MAKE_HHDM(ring_phys);
	memset(ring, 0, PAGE_SIZE);

	r->ring_phys = ring_phys;
	r->ring = ring;
	r->ctrl = ctrl;
	r->size = PAGE_SIZE / sizeof(xhci_trb_t);
	r->index = 0;
	r->cycle = true;

	if (event_ring) {
		r->submissions = NULL;
	} else {
		r->submissions = alloc(sizeof(xhci_submission_t) * r->size);
		if (r->submissions == NULL) {
			mm_release_page(ring_phys);
			return ENOMEM;
		}

		resource_allocator_init(&r->trb_allocator, r->size - 1, r->size - 1);
	}

	MUTEX_INIT(&r->lock);

	return 0;
}

void xhci_free_ring(xhci_ring_t *ring) {
	mm_release_page(ring->ring_phys);
	free(ring->submissions);
}

void xhci_ring_reserve(xhci_ring_t *r, size_t needed) {
	if (needed == 0)
		return;

	__assert(needed <= r->size - 1)

	resource_allocate(&r->trb_allocator, needed);
}

void xhci_ring_unreserve(xhci_ring_t *r, size_t needed) {
	if (needed)
		resource_free(&r->trb_allocator, needed);
}

xhci_submission_t *xhci_ring_submit_locked(xhci_ring_t *r, xhci_trb_t *trb, xhci_submission_t *sub) {
	// Cycle bit must be 0 in the TRB being submitted.
	__assert((trb->dw3 & XHCI_TRB_DW3_C) == 0);

	if (r->index == r->size - 1) {
		// If we're at the end of the ring, we need to set up a link TRB.
		xhci_trb_t *link_trb = &r->ring[r->index];
		link_trb->parameters = (uint64_t)r->ring_phys;
		link_trb->dw2 = 0;
		link_trb->dw3 = XHCI_TRB_DW3_TYPE(TRB_LINK) | XHCI_TRB_DW3_TC | (r->cycle ? XHCI_TRB_DW3_C : 0);

		// Now wrap around to start of ring and flip cycle bit.
		r->index = 0;
		r->cycle ^= 1;
	}

	size_t idx = r->index++;
	xhci_trb_t *r_trb = &r->ring[idx];
	xhci_submission_t *r_sub = NULL;

	memcpy(r_trb, trb, sizeof(xhci_trb_t));

	if (sub) {
		r_sub = &r->submissions[idx];

		bool old_valid = __atomic_exchange_n(&r_sub->valid, true, __ATOMIC_SEQ_CST);
		__assert(!old_valid);

		r_sub->unlock_pages = sub->unlock_pages;

		r_sub->ring = r;
		r_sub->device = sub->device;
		r_sub->callback = sub->callback;
		r_sub->callback_ctx = sub->callback_ctx;
		r_sub->data_length = sub->data_length;
		r_sub->data_offset = sub->data_offset;
		r_sub->next = NULL;
		r_sub->td_head = NULL;
		memcpy(&r_sub->cmd_trb, trb, sizeof(xhci_trb_t));
		r_sub->usb_status = USB_STATUS_SUCCESS; // initialize this to a known value to allow the handling of short packets on control transfers
	}

	// Set cycle bit appropriately.
	if (r->cycle)
		r_trb->dw3 |= XHCI_TRB_DW3_C;

	return r_sub;
}

void xhci_ring_submit(xhci_ring_t *r, xhci_trb_t *trb, xhci_submission_t *sub) {
	xhci_ring_reserve(r, 1);

	MUTEX_ACQUIRE(&r->lock);
	xhci_ring_submit_locked(r, trb, sub);
	MUTEX_RELEASE(&r->lock);
}

xhci_trb_t *xhci_ring_dequeue(xhci_ring_t *r) {
	xhci_trb_t *r_trb = &r->ring[r->index];

	// Make sure the cycle bit matches.
	if (((r_trb->dw3 & XHCI_TRB_DW3_C) != 0) != r->cycle) {
		return NULL;
	}

	// Advance offset and flip cycle bit if needed.
	r->index++;

	if (r->index == r->size) {
		r->index = 0;
		r->cycle ^= 1;
	}

	return r_trb;
}
