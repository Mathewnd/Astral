#ifndef _XHCI_H
#define _XHCI_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <kernel/usb.h>
#include <kernel/pci.h>
#include <kernel/dpc.h>
#include <bitmap.h>
#include <mutex.h>
#include <resource_allocator.h>
#include <ringbuffer.h>
#include <spinlock.h>

#define XHCI_CALLBACK_RING_ENTRIES 256

typedef struct {
	union {
		uint64_t parameters;

		struct {
			uint32_t dw0;
			uint32_t dw1;
		};
	};

	uint32_t dw2;
	uint32_t dw3;
} xhci_trb_t;

typedef struct {
	uint8_t caplength;
	uint8_t reserved1;
	uint16_t hciversion;
	uint32_t hcsparams1;
	uint32_t hcsparams2;
	uint32_t hcsparams3;
	uint32_t hccparams1;
	uint32_t dboff;
	uint32_t rtsoff;
	uint32_t hccparams2;
} xhci_caps_t;

typedef struct {
	uint32_t portsc;
	uint32_t portpmsc;
	uint32_t portli;
	uint32_t porthlpmc;
} xhci_port_regs_t;

typedef struct {
	uint32_t usbcmd;
	uint32_t usbsts;
	uint32_t pagesize;
	uint32_t reserved1;
	uint32_t reserved2;
	uint32_t dnctrl;
	uint64_t crcr;
	uint32_t reserved3[4];
	uint64_t dcbaap;
	uint32_t config;
} xhci_opregs_t;

typedef struct {
	uint32_t iman;
	uint32_t imod;
	uint32_t erstsz;
	uint32_t reserved;
	uint64_t erstba;
	uint64_t erdp;
} xhci_ir_t;

typedef struct {
	uint32_t mfindex;
	uint32_t reserved[7];
	xhci_ir_t ir[1024];
} xhci_rtregs_t;

typedef struct {
	uint64_t ring_segment;
	uint16_t ring_segment_size;
	uint16_t reserved1;
	uint32_t reserved2;
} xhci_erst_entry_t;

typedef struct {
	struct {
		uint32_t route_string : 20;
		uint8_t speed : 4;
		uint8_t reserved1 : 1;
		uint8_t mtt : 1;
		uint8_t hub : 1;
		uint8_t ctx_entries : 5;
	} dw0;

	struct {
		uint16_t max_exit_latency;
		uint8_t root_hub_port_number;
		uint8_t number_of_ports;
	} dw1;

	struct {
		uint8_t tt_hub_slot_id;
		uint8_t tt_port_number;
		uint16_t ttt : 2;
		uint16_t reserved1 : 4;
		uint16_t interrupter_target : 10;
	} dw2;

	struct {
		uint8_t usb_device_address;
		uint8_t reserved1[2];
		uint8_t reserved2 : 3;
		uint8_t slot_state : 5;
	} dw3;

	uint32_t reserved[4];
} xhci_slot_ctx_t;

typedef struct {
	struct {
		uint8_t ep_state : 3;
		uint8_t reserved1 : 5;
		uint8_t mult : 2;
		uint8_t max_pstreams : 5;
		uint8_t lsa : 1;
		uint8_t interval;
		uint8_t max_esit_payload_hi;
	} dw0;

	struct {
		uint8_t reserved1 : 1;
		uint8_t cerr : 2;
		uint8_t ep_type : 3;
		uint8_t reserved2 : 1;
		uint8_t hid : 1;
		uint8_t max_burst_size;
		uint16_t max_packet_size;
	} dw1;

	struct {
		uint32_t dcs : 1;
		uint32_t reserved1 : 3;
		uint32_t tr_dequeue_pointer_lo : 28;
	} dw2;

	struct {
		uint32_t tr_dequeue_pointer_hi;
	} dw3;

	struct {
		uint16_t max_esit_payload_lo;
	} dw4;

	uint32_t reserved[3];
} xhci_ep_ctx_t;

typedef struct {
	uint32_t d;
	uint32_t a;
	uint32_t reserved[5];

	struct {
		uint8_t configuration_value;
		uint8_t interface_number;
		uint8_t alternate_setting;
		uint8_t reserved1;
	} dw7;
} xhci_input_ctx_t;

#define XHCI_CONTEXT_SIZE 32
_Static_assert(sizeof(xhci_slot_ctx_t) == XHCI_CONTEXT_SIZE, "xHCI slot context must be 32 bytes");
_Static_assert(sizeof(xhci_ep_ctx_t) == XHCI_CONTEXT_SIZE, "xHCI endpoint context must be 32 bytes");
_Static_assert(sizeof(xhci_input_ctx_t) == XHCI_CONTEXT_SIZE, "xHCI input control context must be 32 bytes");

#define XHCI_HCCPARAMS1_AC64 (1 << 0)
#define XHCI_HCCPARAMS1_CSZ (1 << 2)

#define XHCI_USBCMD_RS (1 << 0)
#define XHCI_USBCMD_HCRST (1 << 1)
#define XHCI_USBCMD_INTE (1 << 2)

#define XHCI_USBSTS_HCH (1 << 0)
#define XHCI_USBSTS_EINT (1 << 3)
#define XHCI_USBSTS_CNR (1 << 11)
#define XHCI_USBSTS_HCE (1 << 12)

#define XHCI_IMAN_IP (1 << 0)
#define XHCI_IMAN_IE (1 << 1)

#define XHCI_CRCR_RCS (1 << 0)

#define XHCI_ERDP_EHB (1 << 3)

#define XHCI_PORTSC_CCS (1 << 0) // current connection status
#define XHCI_PORTSC_PED (1 << 1) // port enabled
#define XHCI_PORTSC_OCA (1 << 3) // over-current active
#define XHCI_PORTSC_PR (1 << 4) // port reset
#define XHCI_PORTSC_PP (1 << 9) // port power
#define XHCI_PORTSC_CSC (1 << 17) // connect status change
#define XHCI_PORTSC_PEC (1 << 18) // port enabled change
#define XHCI_PORTSC_WRC (1 << 19) // warm reset change
#define XHCI_PORTSC_OCC (1 << 20) // over current change
#define XHCI_PORTSC_PRC (1 << 21) // port reset change
#define XHCI_PORTSC_PLC (1 << 22) // port link state change
#define XHCI_PORTSC_CEC (1 << 23) // config error change
#define XHCI_PORTSC_CAS (1 << 24) // cold attach status
#define XHCI_PORTSC_DR (1 << 30) // device removable
#define XHCI_PORTSC_WPR (1u << 31) // warm port reset

#define XHCI_PORTSC_CHANGE_BITS (XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | XHCI_PORTSC_WRC | XHCI_PORTSC_OCC | XHCI_PORTSC_PRC | XHCI_PORTSC_PLC | XHCI_PORTSC_CEC)
#define XHCI_PORTSC_PRESERVE_BITS XHCI_PORTSC_PP

#define XHCI_TRB_DW2_TR_LEN(LEN) (uint32_t)((LEN) & 0x1ffff)
#define XHCI_TRB_DW2_TD_SIZE(SIZE) ((uint32_t)((SIZE) & 0x1f) << 17)

#define XHCI_TRB_DW3_C (1 << 0)
#define XHCI_TRB_DW3_TC (1 << 1)
#define XHCI_TRB_DW3_ISP (1 << 2)
#define XHCI_TRB_DW3_CH (1 << 4)
#define XHCI_TRB_DW3_IOC (1 << 5)
#define XHCI_TRB_DW3_IDT (1 << 6)
#define XHCI_TRB_DW3_TYPE_MASK (0x3f << 10)
#define XHCI_TRB_DW3_DIR (1 << 16)

#define XHCI_TRB_DW3_TYPE(TYPE) (((TYPE) & 0x3f) << 10)
#define XHCI_TRB_DW3_TRT(TRT) (((TRT) & 0x3) << 16)

typedef enum {
	TRB_NORMAL = 1,
	TRB_SETUP_STAGE = 2,
	TRB_DATA_STAGE = 3,
	TRB_STATUS_STAGE = 4,
	TRB_LINK = 6,
	TRB_ENABLE_SLOT = 9,
	TRB_DISABLE_SLOT = 10,
	TRB_ADDRESS_DEVICE = 11,
	TRB_CONFIGURE_EP = 12,
	TRB_EVALUATE_CTX = 13,
	TRB_RESET_EP = 14,
	TRB_STOP_EP = 15,
	TRB_XFER_COMPLETION_EVENT = 32,
	TRB_COMMAND_COMPLETION_EVENT = 33,
	TRB_PORT_STATUS_CHANGE_EVENT = 34,
} xhci_trb_type_t;

typedef enum {
        TRB_INVALID                    = 0,
        TRB_SUCCESS                    = 1,
        TRB_DATA_BUFFER_ERROR          = 2,
        TRB_BABBLE_DETECTED            = 3,
        TRB_TRANSACTION_ERROR          = 4,
        TRB_TRB_ERROR                  = 5,
        TRB_STALL                      = 6,
        TRB_RESOURCE_ERROR             = 7,
        TRB_BANDWIDTH_ERROR            = 8,
        TRB_NO_SLOTS_AVAILABLE         = 9,
        TRB_INVALID_STREAM_TYPE        = 10,
        TRB_SLOT_NOT_ENABLED           = 11,
        TRB_ENDPOINT_NOT_ENABLED       = 12,
        TRB_SHORT_PACKET               = 13,
        TRB_RING_UNDERRUN              = 14,
        TRB_RING_OVERRUN               = 15,
        TRB_VF_EVENT_RING_FULL         = 16,
        TRB_PARAMETER_ERROR            = 17,
        TRB_BANDWIDTH_OVERRUN_ERROR    = 18,
        TRB_CONTEXT_STATE_ERROR        = 19,
        TRB_NO_PING_RESPONSE           = 20,
        TRB_EVENT_RING_FULL            = 21,
        TRB_INCOMPATIBLE_DEVICE_ERROR  = 22,
        TRB_MISSED_SERVICE             = 23,
        TRB_COMMAND_RING_STOPPED       = 24,
        TRB_COMMAND_RING_ABORTED       = 25,
        TRB_STOPPED                    = 26,
        TRB_STOPPED_LEN                = 27,
        TRB_STOPPED_SHORT              = 28,
        TRB_LATENCY_TOO_LARGE          = 29,
        TRB_ERROR_RESERVED             = 30,
        TRB_BUFFER_OVERRUN             = 31,
        TRB_EVENT_LOST                 = 32,
        TRB_ERROR_UNDEFINED            = 33,
        TRB_INVALID_STREAM_ID          = 34,
        TRB_SECONDARY_BANDWIDTH_ERROR  = 35,
        TRB_SPLIT_TRANSACTION_ERROR    = 36,

        TRB_VENDOR_ERROR_START         = 192,
        TRB_VENDOR_ERROR_END           = 223,
        TRB_VENDOR_INFO_START          = 224,
        TRB_VENDOR_INFO_END            = 255,
} xhci_trb_status_t;

typedef enum {
	XHCI_PORT_SPEED_FULL = 1,
	XHCI_PORT_SPEED_LOW = 2,
	XHCI_PORT_SPEED_HIGH = 3,
	XHCI_PORT_SPEED_SUPER = 4,
	XHCI_PORT_SPEED_SS_2X1 = 5,
	XHCI_PORT_SPEED_SS_1X2 = 6,
	XHCI_PORT_SPEED_SS_2X2 = 7,
} xhci_port_speed_t;

typedef enum {
	XHCI_EP_TYPE_ISOCH_OUT = 1,
	XHCI_EP_TYPE_BULK_OUT = 2,
	XHCI_EP_TYPE_INTR_OUT = 3,
	XHCI_EP_TYPE_CTRL = 4,
	XHCI_EP_TYPE_ISOCH_IN = 5,
	XHCI_EP_TYPE_BULK_IN = 6,
	XHCI_EP_TYPE_INTR_IN = 7,
} xhci_ep_type_t;

typedef struct xhci_submission xhci_submission_t;
typedef struct xhci_device xhci_device_t;

typedef struct {
	uint8_t ver_major;
	uint8_t ver_minor;
} xhci_port_protocol_t;

struct xhci_submission {
	list_node_t list_node;
	// Set whenever a submission is queued
	bool valid;
	bool unlock_pages;
	struct xhci_ring *ring;

	// The data associated with this submission
	usb_device_t *device;
	usb_completion_callback_t callback;
	void *callback_ctx;
	size_t data_length;
	size_t data_offset;
	xhci_submission_t *next;
	xhci_submission_t *td_head;

	// event_trb is filled from the event ring; cmd_trb is initialized on submission.
	xhci_trb_t event_trb;
	xhci_trb_t cmd_trb;
	usb_status_t usb_status;
};

typedef struct xhci_ring {
	void *ring_phys;
	xhci_trb_t *ring;

	// Mutex to protect the ring structure.
	mutex_t lock;

	// Back-pointer to the controller.
	struct xhci_ctrl *ctrl;

	// List of submissions on this ring. Will be NULL for event rings.
	xhci_submission_t *submissions;
	resource_allocator_t trb_allocator;

	// Ring size in TRBs.
	size_t size;

	// For event rings, the index of the next event to process.
	// For command and transfer rings, the index of the next free TRB.
	size_t index;

	// Cycle bit for the ring.
	bool cycle;
} xhci_ring_t;

typedef struct {
	usb_device_t *device;
	usb_completion_callback_t callback;
	void *callback_ctx;
	usb_status_t status;
	size_t transferred;
} xhci_completion_t;

typedef struct xhci_ctrl {
	usb_ctrl_t ctrl;
	pcienum_t *pci_enum;

	list_t hubs;

	uint32_t port_count;
	uint32_t slot_count;

	xhci_port_protocol_t *ports;
	xhci_device_t **slots;

	xhci_ring_t command_ring;
	xhci_ring_t event_ring;

	semaphore_t event_sem;
	semaphore_t callback_sem;
	semaphore_t callback_space_sem;
	spinlock_t event_lock;
	mutex_t callback_mutex;
	bitmap_t port_change_bitmap;
	list_t completion_list;
	ringbuffer_t callback_ring;
	dpc_t dpc;

	uint32_t ctx_stride;

	volatile xhci_caps_t *caps;
	volatile xhci_opregs_t *opregs;
	volatile xhci_rtregs_t *rtregs;
	volatile xhci_port_regs_t *portregs;

	volatile uint64_t *dcbaa;
	volatile uint32_t *dbs;
} xhci_ctrl_t;

typedef struct {
	usb_hub_t hub;
	uint8_t port_offset;
	list_node_t list_node;
} xhci_root_hub_t;

struct xhci_device {
	usb_device_t device;
	xhci_ring_t ep_rings[31];

	uint32_t route_string;
	uint8_t device_tier;
	uint8_t slot_id;
	uint8_t ctrl_speed;
	uint8_t root_port_number;

	void *device_ctx_phys;
	void *device_ctx;
	void *input_ctx_phys;
	void *input_ctx;
};

void xhci_handle_port_change_event(xhci_ctrl_t *xhci, int global_port);
int xhci_data_xfer(xhci_ctrl_t *xhci, xhci_device_t *dev, usb_xfer_t *xfer, xhci_submission_t *sub);
int xhci_sg_data_xfer(xhci_ctrl_t *xhci, xhci_ring_t *ring, xhci_device_t *dev, usb_xfer_t *xfer, xhci_submission_t *sub);
int xhci_control_xfer(xhci_ctrl_t *xhci, xhci_device_t *dev, usb_xfer_t *xfer, xhci_submission_t *sub);
xhci_trb_t *xhci_ring_dequeue(xhci_ring_t *r);
void xhci_ring_reserve(xhci_ring_t *r, size_t needed);
void xhci_ring_unreserve(xhci_ring_t *r, size_t needed);
void xhci_ring_submit(xhci_ring_t *r, xhci_trb_t *trb, xhci_submission_t *sub);
xhci_submission_t *xhci_ring_submit_locked(xhci_ring_t *r, xhci_trb_t *trb, xhci_submission_t *sub);
int xhci_alloc_ring(xhci_ctrl_t *ctrl, xhci_ring_t *r, bool event_ring);
void xhci_free_ring(xhci_ring_t *ring);

static inline void xhci_ring_command_doorbell(xhci_ctrl_t *ctrl) {
	ctrl->dbs[0] = 0;
}

static inline void xhci_ring_endpoint_doorbell(xhci_ctrl_t *ctrl, xhci_device_t *device, int endpoint) {
	ctrl->dbs[device->slot_id] = endpoint;
}

extern usb_hub_ops_t xhci_root_hub_ops;
extern usb_ctrl_ops_t xhci_ops;

#endif
