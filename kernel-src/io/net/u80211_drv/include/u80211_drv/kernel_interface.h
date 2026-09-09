#ifndef U80211_DRV_KERNEL_INTERFACE_H
#define U80211_DRV_KERNEL_INTERFACE_H

#include <stddef.h>
#include <stdint.h>

// opaque handles supplied by the kernel integration
// the driver only stores and passes these handles back to the integration
typedef void *u80211_drv_device_handle_t;
typedef void *u80211_drv_interface_handle_t;
typedef void *u80211_drv_transfer_handle_t;
typedef void *u80211_drv_network_device_handle_t;

typedef void (*u80211_drv_kernel_transfer_callback_t)(void *context, int status, size_t transferred_size);

// usb device descriptor fields used to match a driver
typedef struct {
	uint16_t vendor_id;
	uint16_t product_id;
} u80211_drv_device_descriptor_t;

// usb interface descriptor fields used to match a driver and discover its endpoints
typedef struct {
	uint8_t number;
	uint8_t class_code;
	uint8_t subclass;
	uint8_t protocol;
	uint8_t endpoint_count;
} u80211_drv_interface_descriptor_t;

// usb endpoint descriptor fields required by a driver
typedef struct {
	uint8_t address;
	uint8_t attributes;
	uint16_t maximum_packet_size;
	uint8_t interval;
} u80211_drv_endpoint_descriptor_t;

#define U80211_DRV_DEVICE_MAC_ADDRESS_LEN 6
#define U80211_DRV_DEVICE_RATE_BITMAP_LEN 16

// radio properties published once initialization has completed
typedef struct {
	uint8_t mac_address[U80211_DRV_DEVICE_MAC_ADDRESS_LEN];
	// bit N represents a supported rate of N * 500 kbit/s
	uint8_t rate_bitmap[U80211_DRV_DEVICE_RATE_BITMAP_LEN];
} u80211_drv_device_metadata_t;

// cipher values used by key installation and transmission
#define U80211_DRV_CIPHER_NONE -1
#define U80211_DRV_CIPHER_CCMP 0
#define U80211_DRV_CIPHER_TKIP 1
#define U80211_DRV_CIPHER_WEP40 2
#define U80211_DRV_CIPHER_WEP104 3

#define U80211_DRV_KEY_PAIRWISE (1u << 0)
#define U80211_DRV_KEY_GROUP (1u << 1)
#define U80211_DRV_KEY_RX (1u << 2)
#define U80211_DRV_KEY_TX (1u << 3)

// key data passed to set_key
typedef struct {
	int cipher;
	uint8_t index;
	uint8_t peer[U80211_DRV_DEVICE_MAC_ADDRESS_LEN];
	const uint8_t *key;
	size_t key_len;
	uint32_t flags;
} u80211_drv_key_t;

// per-packet encryption selection
// key is the installed key index or -1 when no key is selected
// cipher is one of U80211_DRV_CIPHER_*
typedef struct {
	int key;
	int cipher;
} u80211_drv_transmit_options_t;

// operations implemented by a hardware driver and published through u80211_drv_device_ready()
// unless otherwise stated operations return a U80211_DRV_STATUS_* value
typedef struct {
	// allocates a buffer of 'size' bytes
	// on success '*buffer' points at the start of the writable 802.11 frame region
	int (*allocate_tx_buffer)(size_t size, void **buffer);
	void (*free_tx_buffer)(void *buffer);
	// transmits the bytes and consumes buffer including on failure
	int (*transmit)(void *device, void *buffer, size_t size, size_t current_offset, const u80211_drv_transmit_options_t *options);
	// sets the rf channel number the device will rx/tx on
	int (*set_channel)(void *device, uint8_t channel);
	// adds an encryption key
	int (*set_key)(void *device, const u80211_drv_key_t *key);
	// deletes an encryption ke
	int (*del_key)(void *device, uint8_t index);
} u80211_drv_device_ops_t;

// values used to control usb transfers
#define U80211_DRV_KERNEL_XFER_OUT 0x00
#define U80211_DRV_KERNEL_XFER_IN 0x80
#define U80211_DRV_KERNEL_XFER_DIRECTION_MASK 0x80

#define U80211_DRV_KERNEL_ENDPOINT_TRANSFER_TYPE_MASK 0x03
#define U80211_DRV_KERNEL_ENDPOINT_TRANSFER_TYPE_BULK 0x02

#define U80211_DRV_KERNEL_XFER_REQUEST_TYPE_STANDARD 0x00
#define U80211_DRV_KERNEL_XFER_REQUEST_TYPE_CLASS 0x20
#define U80211_DRV_KERNEL_XFER_REQUEST_TYPE_VENDOR 0x40
#define U80211_DRV_KERNEL_XFER_REQUEST_TYPE_RESERVED 0x60
#define U80211_DRV_KERNEL_XFER_REQUEST_TYPE_MASK 0x60

#define U80211_DRV_KERNEL_XFER_RECIPIENT_DEVICE 0x00
#define U80211_DRV_KERNEL_XFER_RECIPIENT_INTERFACE 0x01
#define U80211_DRV_KERNEL_XFER_RECIPIENT_ENDPOINT 0x02
#define U80211_DRV_KERNEL_XFER_RECIPIENT_OTHER 0x03
#define U80211_DRV_KERNEL_XFER_RECIPIENT_MASK 0x1f

#define U80211_DRV_KERNEL_PRINT_LEVEL_INFO 0
#define U80211_DRV_KERNEL_PRINT_LEVEL_WARN 1
#define U80211_DRV_KERNEL_PRINT_LEVEL_ERROR 2

// allocates 'size' bytes of memory
// return NULL on allocation failure
void *u80211_drv_kernel_allocate(size_t size);

// frees the kernel memory referenced by 'memory'
// 'memory' will never be NULL
void u80211_drv_kernel_free(void *memory);

// requests for firmware to be loaded
// invokes callback once firmware is available
// the callback must not be invoked on failure
// 'firmware_data' only needs to be valid for the duration of the callback
typedef void (*u80211_drv_kernel_firmware_callback_t)(void *context, const void *firmware_data, size_t firmware_size);
int u80211_drv_kernel_get_firmware(const char *name, u80211_drv_kernel_firmware_callback_t callback, void *context);

// these functions return usb descriptors into the specified buffers
int u80211_drv_kernel_get_device_descriptor(u80211_drv_device_handle_t device, u80211_drv_device_descriptor_t *descriptor);
int u80211_drv_kernel_get_interface_descriptor(u80211_drv_interface_handle_t interface, u80211_drv_interface_descriptor_t *descriptor);
int u80211_drv_kernel_get_endpoints(u80211_drv_interface_handle_t interface, u80211_drv_endpoint_descriptor_t *endpoints, size_t endpoint_count);

// synchronous usb control transfers with a timeout in milliseconds
// 'flags' combines U80211_DRV_KERNEL_XFER_* values into the usb control request type
int u80211_drv_kernel_submit_control_xfer_and_wait(u80211_drv_device_handle_t device, uint8_t flags, uint8_t request, uint16_t value, uint16_t index, void *buf, uint16_t buffer_size, size_t *transferred_size, unsigned int timeout);

// synchronous usb bulk transfers with a timeout in milliseconds
int u80211_drv_kernel_submit_bulk_xfer_and_wait(u80211_drv_device_handle_t device, uint8_t endpoint_address, void *buf, size_t buffer_size, size_t *transferred_size, unsigned int timeout);

// allocates a reusable asynchronous bulk transfer
// 'buffer' and 'context' must stay valid across submissions and callbacks
// callback receives a U80211_DRV_STATUS_* value and zero transferred_size on failure
// a completed transfer may be submitted again from its callback
int u80211_drv_kernel_allocate_bulk_xfer(u80211_drv_device_handle_t device, uint8_t endpoint_address, void *buffer, size_t buffer_size, u80211_drv_kernel_transfer_callback_t callback, void *context, u80211_drv_transfer_handle_t *transfer);

// starts an asynchronous transfer
// submitting one that is already active must return U80211_DRV_STATUS_INVALID_ARGUMENT
int u80211_drv_kernel_submit_xfer(u80211_drv_transfer_handle_t transfer);

// busy-waits for at least 'microseconds' before returning
void u80211_drv_kernel_stall_us(unsigned int microseconds);

// writes one complete message at the requested U80211_DRV_KERNEL_PRINT_LEVEL_*
void u80211_drv_kernel_print(int level, const char *msg);

// publishes an initialized radio to the kernel integration
// 'network_device' receives the handle used for received packets
int u80211_drv_device_ready(void *device, const u80211_drv_device_metadata_t *metadata, const u80211_drv_device_ops_t *ops, u80211_drv_network_device_handle_t *network_device);

// delivers one 802.11 packet
// might be called from an interrupt context
void u80211_drv_packet_received(u80211_drv_network_device_handle_t device, void *packet, size_t packet_size);

#endif
