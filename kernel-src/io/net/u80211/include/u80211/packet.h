#ifndef U80211_PACKET_H
#define U80211_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct u80211_device u80211_device_t;

typedef struct {
	void *data;
	size_t size;
	size_t current_offset;
} u80211_tx_buffer_descriptor_t;

typedef struct {
	int key;
	int cipher;
} u80211_transmit_options_t;

typedef struct {
	uint8_t bytes[6];
} u80211_mac_address_t;

static inline bool u80211_mac_address_equal(const u80211_mac_address_t *a, const u80211_mac_address_t *b) {
	for (size_t i = 0; i < sizeof(a->bytes); ++i) {
		if (a->bytes[i] != b->bytes[i])
			return false;
	}

	return true;
}

#define U80211_HEADER_FRAME_CONTROL_GET_VERSION(x) ((x) & 0x3)
#define U80211_HEADER_FRAME_CONTROL_GET_TYPE(x) (((x) & 0xc) >> 2)
#define 	U80211_HEADER_FRAME_CONTROL_TYPE_MANAGEMENT 0
#define 	U80211_HEADER_FRAME_CONTROL_TYPE_CONTROL 1
#define 	U80211_HEADER_FRAME_CONTROL_TYPE_DATA 2
#define U80211_HEADER_FRAME_CONTROL_GET_SUBTYPE(x) (((x) & 0xf0) >> 4)
#define 	U80211_HEADER_FRAME_CONTROL_SUBTYPE_ASSOCIATION_REQUEST 0
#define 	U80211_HEADER_FRAME_CONTROL_SUBTYPE_ASSOCIATION_RESPONSE 1
#define 	U80211_HEADER_FRAME_CONTROL_SUBTYPE_PROBE_REQUEST 4
#define 	U80211_HEADER_FRAME_CONTROL_SUBTYPE_PROBE_RESPONSE 5
#define 	U80211_HEADER_FRAME_CONTROL_SUBTYPE_DISASSOCIATION 10
#define 	U80211_HEADER_FRAME_CONTROL_SUBTYPE_AUTHENTICATION 11
#define 	U80211_HEADER_FRAME_CONTROL_SUBTYPE_DEAUTHENTICATION 12
#define 	U80211_HEADER_FRAME_CONTROL_SUBTYPE_DATA 0
#define 	U80211_HEADER_FRAME_CONTROL_SUBTYPE_NULL_DATA 4
#define U80211_HEADER_FRAME_CONTROL_TO_DS 0x100
#define U80211_HEADER_FRAME_CONTROL_FROM_DS 0x200
#define U80211_HEADER_FRAME_CONTROL_MORE_FRAGMENTS 0x400
#define U80211_HEADER_FRAME_CONTROL_RETRY 0x800
#define U80211_HEADER_FRAME_CONTROL_POWER_MANAGEMENT 0x1000
#define U80211_HEADER_FRAME_CONTROL_MORE_DATA 0x2000
#define U80211_HEADER_FRAME_CONTROL_PROTECTED_FRAME 0x4000
#define U80211_HEADER_FRAME_CONTROL_ORDER 0x8000

typedef struct {
	uint16_t frame_control;
	uint16_t duration_id;
	u80211_mac_address_t addresses[4];
	uint16_t sequence_control;
} u80211_header_description_t;

typedef struct {
	u80211_mac_address_t mac_address;
	uint16_t interval;
	uint16_t capabilities;
	uint8_t channel;
	uint8_t rate_bitmap[16];
	char ssid[33];
	const uint8_t *rsn;
	size_t rsn_size;
} u80211_beacon_data_t;

#define U80211_AUTH_ALGORITHM_OPEN 0
typedef struct {
	u80211_mac_address_t address;
	uint16_t auth_algorithm;
	uint16_t auth_transaction;
	uint16_t status;
} u80211_auth_data_t;

typedef struct {
	u80211_mac_address_t address;
	uint16_t capabilities;
	uint16_t status;
	uint16_t association_id;
	uint8_t rate_bitmap[16];
} u80211_association_response_data_t;

typedef struct {
	u80211_mac_address_t address;
	uint16_t reason;
} u80211_deauthentication_data_t;

typedef struct {
	u80211_mac_address_t address;
	uint16_t reason;
} u80211_disassociation_data_t;

int u80211_deserialize_header(void *source, size_t source_size, u80211_header_description_t *header, void **data_start, size_t *data_size);
int u80211_serialize_header(u80211_header_description_t *header, u80211_tx_buffer_descriptor_t *descriptor);
void u80211_process_management_packet(u80211_device_t *device, u80211_header_description_t *header, const void *data, size_t data_size); // called from an interrupt context
void u80211_process_data_packet(u80211_device_t *device, u80211_header_description_t *header, void *data, size_t data_size); // called from an interrupt context

int u80211_send_probe_request(u80211_device_t *device);
int u80211_send_authentication(u80211_device_t *device, u80211_auth_data_t *auth_data);
int u80211_send_deauthentication(u80211_device_t *device, u80211_deauthentication_data_t *deauthentication_data);
int u80211_send_association_request(u80211_device_t *device, const void *information_elements, size_t information_elements_size);

#endif
