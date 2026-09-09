#ifndef U80211_U80211_H
#define U80211_U80211_H

#include <stdbool.h>
#include <stddef.h>
#include <u80211/bss_cache.h>
#include <u80211/list.h>
#include <u80211/packet.h>
#include <u80211/ap.h>
#include <u80211/key.h>
#include <u80211/status.h>

static inline void *u80211_descriptor_allocate_space(u80211_tx_buffer_descriptor_t *descriptor, size_t size) {
	if (size > descriptor->current_offset)
		return NULL;

	descriptor->current_offset -= size;
	return (uint8_t *)descriptor->data + descriptor->current_offset;
}

typedef struct {
	int (*allocate_tx_buffer)(u80211_device_t *device, size_t size, u80211_tx_buffer_descriptor_t *buffer_descriptor);
	int (*free_tx_buffer)(u80211_device_t *device, u80211_tx_buffer_descriptor_t *buffer_descriptor);
	int (*transmit)(u80211_device_t *device, u80211_tx_buffer_descriptor_t *buffer_descriptor, const u80211_transmit_options_t *options);
	int (*set_channel)(u80211_device_t *device, int channel);
	int (*set_key)(u80211_device_t *device, const u80211_key_t *key);
	int (*del_key)(u80211_device_t *device, uint8_t index, const u80211_mac_address_t *peer, uint32_t flags);
} u80211_device_ops_t;

typedef struct {
	u80211_mac_address_t mac_address;
	uint8_t rate_bitmap[16];
} u80211_device_metadata_t;

#define U80211_DEVICE_STATE_DOWN 0
#define U80211_DEVICE_STATE_SCANNING 1
#define U80211_DEVICE_STATE_AUTHENTICATING 2
#define U80211_DEVICE_STATE_ASSOCIATING 3
#define U80211_DEVICE_STATE_ASSOCIATED 4
#define U80211_DEVICE_STATE_DEAUTHENTICATING 5
struct u80211_device {
	u80211_device_metadata_t metadata;
	void *driver_data;
	const u80211_device_ops_t *ops;

	int state;
	size_t packet_count;
	uint16_t tx_sequence_control;
	uint16_t received_sequence_control;
	bool received_sequence_control_valid;
	void *key_spinlock;
	u80211_list_t keys;

	void *scan_spinlock;
	void *scan_context;
	void *scan_cleanup_work;
	u80211_list_t scan_waiters;

	bss_cache_t bss_cache;

	void *association_spinlock;
	void *association_context;
	void *association_cleanup_work;
	void *association_cleanup_context;
	bool association_cleanup_pending;
	u80211_list_t association_waiters;
	unsigned int association_generation;
	int association_result;
	u80211_ap_t *ap;
	u80211_ap_t *disconnected_ap;
};

static inline int u80211_get_device_state(u80211_device_t *device) {
	return __atomic_load_n(&device->state, __ATOMIC_ACQUIRE);
}

static inline bool u80211_set_device_state(u80211_device_t *device, int old_state, int new_state) {
	return __atomic_compare_exchange_n(&device->state, &old_state, new_state, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

int u80211_scan(u80211_device_t *device);
int u80211_wait_for_scan_completion(u80211_device_t *device);

void u80211_process_packet(u80211_device_t *device, void *packet, size_t packet_size);

// these are mostly diagnostic and should not be relied upon
size_t u80211_get_packet_count(u80211_device_t *device);
void u80211_reset_packet_count(u80211_device_t *device);

int u80211_register_device(const u80211_device_metadata_t *metadata, const u80211_device_ops_t *ops, void *driver_data, u80211_device_t **device_out);
void u80211_unregister_device(u80211_device_t *device);

int u80211_associate(u80211_device_t *device, u80211_ap_t *ap, const void *information_elements, size_t information_elements_size);
int u80211_wait_for_association_completion(u80211_device_t *device);
int u80211_disassociate(u80211_device_t *device);

// allocates an ethernet-sized buffer
int u80211_allocate_tx_buffer(u80211_device_t *device, u80211_tx_buffer_descriptor_t *descriptor);
// expects an ethernet header. descriptor **must** be a buffer returned by u80211_allocate_tx_buffer.
// consumes the descriptor regardless of the returned status.
int u80211_transmit_buffer(u80211_device_t *device, u80211_tx_buffer_descriptor_t *descriptor);

#endif
