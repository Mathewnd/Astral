#ifndef U80211_KEY_H
#define U80211_KEY_H

#include <stddef.h>
#include <stdint.h>
#include <u80211/packet.h>

typedef enum {
	U80211_CIPHER_CCMP,
	U80211_CIPHER_TKIP,
	U80211_CIPHER_WEP40,
	U80211_CIPHER_WEP104,
} u80211_cipher_t;

typedef enum {
	U80211_KEY_PAIRWISE = 1 << 0,
	U80211_KEY_GROUP = 1 << 1,
	U80211_KEY_RX = 1 << 2,
	U80211_KEY_TX = 1 << 3,
} u80211_key_flags_t;

typedef struct {
	u80211_cipher_t cipher;
	uint8_t index;
	u80211_mac_address_t peer;
	const uint8_t *key;
	size_t key_len;
	const uint8_t *rx_seq;
	size_t rx_seq_len;
	uint32_t flags;
} u80211_key_t;

typedef struct {
	const u80211_header_description_t *header;
	uint8_t key_index;
	const u80211_mac_address_t *destination;
	const u80211_mac_address_t *source;
	uint8_t priority;
	const void *data;
	size_t data_size;
	uint8_t sequence[6];
	uint8_t mic[8];
} u80211_tkip_data_t;

int u80211_set_key(u80211_device_t *device, const u80211_key_t *key);
int u80211_del_key(u80211_device_t *device, uint8_t index, const u80211_mac_address_t *peer, uint32_t flags);
int u80211_select_key(u80211_device_t *device, const u80211_header_description_t *header);
int u80211_select_cipher(u80211_device_t *device, const u80211_header_description_t *header);
int u80211_select_cipher_by_index(u80211_device_t *device, const u80211_header_description_t *header, uint8_t index);
bool u80211_key_update_rx_sequence(u80211_device_t *device, const u80211_header_description_t *header, uint8_t index, u80211_cipher_t cipher, const uint8_t *sequence, size_t sequence_size);
bool u80211_key_validate_tkip_rx(u80211_device_t *device, const u80211_tkip_data_t *data);
bool u80211_key_prepare_tkip_tx(u80211_device_t *device, u80211_tkip_data_t *data);
bool u80211_key_next_tx_sequence(u80211_device_t *device, const u80211_header_description_t *header, uint8_t index, u80211_cipher_t cipher, uint8_t *sequence, size_t sequence_size);

#endif
