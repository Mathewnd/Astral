#include <u80211/status.h>
#include <u80211/kernel_interface.h>
#include <u80211/michael.h>
#include <u80211/u80211.h>
#include <u80211/util.h>

#define SEQUENCE_SIZE 6
#define TKIP_KEY_SIZE 32
#define TKIP_TX_MIC_KEY_OFFSET 16
#define TKIP_RX_MIC_KEY_OFFSET 24
#define MIC_SIZE 8

typedef struct {
	u80211_list_node_t node;
	u80211_cipher_t cipher;
	uint8_t index;
	u80211_mac_address_t peer;
	uint32_t flags;
	uint8_t rx_sequence[SEQUENCE_SIZE];
	uint8_t tx_sequence[SEQUENCE_SIZE];
	size_t key_len;
	uint8_t key[];
} u80211_key_metadata_t;

static void destroy_key_metadata(u80211_key_metadata_t *metadata) {
	// TODO: proper memset for zeroing crypto data
	u80211_memset(metadata, 0, sizeof(*metadata) + metadata->key_len);
	u80211_kernel_free(metadata);
}

static bool key_identity_equal(const u80211_key_metadata_t *metadata, uint8_t index, const u80211_mac_address_t *peer, uint32_t flags) {
	return metadata->index == index && metadata->flags == flags && u80211_mac_address_equal(&metadata->peer, peer);
}

int u80211_key_state_init(u80211_device_t *device) {
	u80211_list_init(&device->keys);
	device->key_spinlock = u80211_kernel_allocate_spinlock();
	if (device->key_spinlock == NULL)
		return U80211_STATUS_ENOMEM;

	return U80211_STATUS_SUCCESS;
}

void u80211_key_state_deinit(u80211_device_t *device) {
	u80211_list_node_t *node;
	while ((node = u80211_list_pop_front(&device->keys)) != NULL)
		destroy_key_metadata(container_of(node, u80211_key_metadata_t, node));

	u80211_kernel_free_spinlock(device->key_spinlock);
	device->key_spinlock = NULL;
}

int u80211_set_key(u80211_device_t *device, const u80211_key_t *key) {
	if (device->ops->set_key == NULL)
		return U80211_STATUS_UNSUPPORTED;

	if ((key->key == NULL && key->key_len != 0) || key->key_len > SIZE_MAX - sizeof(u80211_key_metadata_t))
		return U80211_STATUS_NOT_PERMITTED;

	u80211_key_metadata_t *metadata = u80211_kernel_allocate(sizeof(*metadata) + key->key_len);
	if (metadata == NULL)
		return U80211_STATUS_ENOMEM;

	metadata->index = key->index;
	metadata->cipher = key->cipher;
	metadata->peer = key->peer;
	metadata->flags = key->flags;
	metadata->key_len = key->key_len;

	u80211_memset(metadata->rx_sequence, 0, sizeof(metadata->rx_sequence));
	u80211_memset(metadata->tx_sequence, 0, sizeof(metadata->tx_sequence));

	if (key->rx_seq != NULL && key->rx_seq_len == sizeof(metadata->rx_sequence))
		u80211_memcpy(metadata->rx_sequence, key->rx_seq, sizeof(metadata->rx_sequence));

	if (key->key_len != 0)
		u80211_memcpy(metadata->key, key->key, key->key_len);

	int status = device->ops->set_key(device, key);
	if (status != U80211_STATUS_SUCCESS) {
		destroy_key_metadata(metadata);
		return status;
	}

	u80211_key_metadata_t *old_metadata = NULL;
	u80211_kernel_acquire_spinlock(device->key_spinlock);
	u80211_list_for_each(&device->keys, node) {
		u80211_key_metadata_t *candidate = container_of(node, u80211_key_metadata_t, node);
		if (key_identity_equal(candidate, key->index, &key->peer, key->flags)) {
			old_metadata = candidate;
			u80211_list_remove(&device->keys, node);
			break;
		}
	}
	u80211_list_push_front(&device->keys, &metadata->node);
	u80211_kernel_release_spinlock(device->key_spinlock);

	if (old_metadata != NULL)
		destroy_key_metadata(old_metadata);
	return U80211_STATUS_SUCCESS;
}

int u80211_del_key(u80211_device_t *device, uint8_t index, const u80211_mac_address_t *peer, uint32_t flags) {
	if (device->ops->del_key == NULL)
		return U80211_STATUS_UNSUPPORTED;

	int status = device->ops->del_key(device, index, peer, flags);
	if (status != U80211_STATUS_SUCCESS)
		return status;

	u80211_key_metadata_t *removed_metadata = NULL;
	u80211_kernel_acquire_spinlock(device->key_spinlock);
	u80211_list_for_each(&device->keys, node) {
		u80211_key_metadata_t *candidate = container_of(node, u80211_key_metadata_t, node);
		if (key_identity_equal(candidate, index, peer, flags)) {
			removed_metadata = candidate;
			u80211_list_remove(&device->keys, node);
			break;
		}
	}
	u80211_kernel_release_spinlock(device->key_spinlock);

	if (removed_metadata != NULL)
		destroy_key_metadata(removed_metadata);
	return U80211_STATUS_SUCCESS;
}

static bool key_matches_header(const u80211_key_metadata_t *metadata, u80211_device_t *device, const u80211_header_description_t *header) {
	bool tx = u80211_mac_address_equal(&header->addresses[1], &device->metadata.mac_address);
	bool group = header->addresses[0].bytes[0] & 1;
	uint32_t direction_flag = tx ? U80211_KEY_TX : U80211_KEY_RX;
	uint32_t type_flag = group ? U80211_KEY_GROUP : U80211_KEY_PAIRWISE;
	const u80211_mac_address_t *peer = tx ? &header->addresses[0] : &header->addresses[1];

	return (metadata->flags & direction_flag) && (metadata->flags & type_flag) && (group || u80211_mac_address_equal(&metadata->peer, peer));
}

// expect key spinlock to be locked
static u80211_key_metadata_t *select_key(u80211_device_t *device, const u80211_header_description_t *header) {
	u80211_list_for_each(&device->keys, node) {
		u80211_key_metadata_t *metadata = container_of(node, u80211_key_metadata_t, node);
		if (key_matches_header(metadata, device, header))
			return metadata;
	}
	return NULL;
}

// expect key spinlock to be locked
static u80211_key_metadata_t *select_key_by_index(u80211_device_t *device, const u80211_header_description_t *header, uint8_t index) {
	u80211_list_for_each(&device->keys, node) {
		u80211_key_metadata_t *metadata = container_of(node, u80211_key_metadata_t, node);
		if (metadata->index == index && key_matches_header(metadata, device, header))
			return metadata;
	}
	return NULL;
}

int u80211_select_key(u80211_device_t *device, const u80211_header_description_t *header) {
	u80211_kernel_acquire_spinlock(device->key_spinlock);
	u80211_key_metadata_t *metadata = select_key(device, header);
	int index = metadata == NULL ? -1 : metadata->index;
	u80211_kernel_release_spinlock(device->key_spinlock);
	return index;
}

int u80211_select_cipher(u80211_device_t *device, const u80211_header_description_t *header) {
	u80211_kernel_acquire_spinlock(device->key_spinlock);

	u80211_key_metadata_t *metadata = select_key(device, header);
	int cipher = -1;
	if (metadata)
		cipher = metadata->cipher;

	u80211_kernel_release_spinlock(device->key_spinlock);
	return cipher;
}

int u80211_select_cipher_by_index(u80211_device_t *device, const u80211_header_description_t *header, uint8_t index) {
	u80211_kernel_acquire_spinlock(device->key_spinlock);

	u80211_key_metadata_t *metadata = select_key_by_index(device, header, index);
	int cipher = -1;
	if (metadata)
		cipher = metadata->cipher;

	u80211_kernel_release_spinlock(device->key_spinlock);
	return cipher;
}

static bool sequence_is_newer(const uint8_t *sequence, const uint8_t *previous, size_t size) {
	for (size_t i = size; i != 0; --i) {
		if (sequence[i - 1] != previous[i - 1])
			return sequence[i - 1] > previous[i - 1];
	}

	return false;
}

static bool mic_equal(const uint8_t a[MIC_SIZE], const uint8_t b[MIC_SIZE]) {
	uint8_t difference = 0;
	for (size_t i = 0; i < MIC_SIZE; ++i)
		difference |= a[i] ^ b[i];
	return difference == 0;
}

bool u80211_key_validate_tkip_rx(u80211_device_t *device, const u80211_tkip_data_t *data) {
	bool valid = false;
	u80211_kernel_acquire_spinlock(device->key_spinlock);

	u80211_key_metadata_t *metadata = select_key_by_index(device, data->header, data->key_index);
	if (metadata == NULL || metadata->cipher != U80211_CIPHER_TKIP || metadata->key_len != TKIP_KEY_SIZE)
		goto unlock;
	if (!sequence_is_newer(data->sequence, metadata->rx_sequence, SEQUENCE_SIZE))
		goto unlock;

	uint8_t calculated_mic[MIC_SIZE];
	u80211_michael_mic(metadata->key + TKIP_RX_MIC_KEY_OFFSET, data->destination->bytes, data->source->bytes,
		data->priority, data->data, data->data_size, calculated_mic);
	if (!mic_equal(calculated_mic, data->mic))
		goto unlock;

	u80211_memcpy(metadata->rx_sequence, data->sequence, SEQUENCE_SIZE);
	valid = true;

unlock:
	u80211_kernel_release_spinlock(device->key_spinlock);
	return valid;
}

bool u80211_key_update_rx_sequence(u80211_device_t *device, const u80211_header_description_t *header, uint8_t index, u80211_cipher_t cipher, const uint8_t *sequence, size_t sequence_size) {
	if (sequence_size != SEQUENCE_SIZE)
		return false;

	u80211_kernel_acquire_spinlock(device->key_spinlock);
	u80211_key_metadata_t *metadata = select_key_by_index(device, header, index);
	bool valid = metadata != NULL && metadata->cipher == cipher && sequence_is_newer(sequence, metadata->rx_sequence, sequence_size);
	if (valid)
		u80211_memcpy(metadata->rx_sequence, sequence, sequence_size);

	u80211_kernel_release_spinlock(device->key_spinlock);
	return valid;
}

static bool increment_sequence(uint8_t *sequence, size_t size) {
	// find where to increment
	size_t carry_end = 0;
	while (carry_end < size && sequence[carry_end] == UINT8_MAX)
		carry_end++;

	// can't increase sequence
	if (carry_end == size)
		return false;

	u80211_memset(sequence, 0, carry_end);
	sequence[carry_end]++;
	return true;
}

bool u80211_key_prepare_tkip_tx(u80211_device_t *device, u80211_tkip_data_t *data) {
	bool valid = false;
	u80211_kernel_acquire_spinlock(device->key_spinlock);

	u80211_key_metadata_t *metadata = select_key_by_index(device, data->header, data->key_index);
	if (metadata == NULL || metadata->cipher != U80211_CIPHER_TKIP || metadata->key_len != TKIP_KEY_SIZE)
		goto unlock;

	uint8_t sequence[SEQUENCE_SIZE];
	u80211_memcpy(sequence, metadata->tx_sequence, sizeof(sequence));
	if (!increment_sequence(sequence, sizeof(sequence)))
		goto unlock;

	uint8_t mic[MIC_SIZE];
	u80211_michael_mic(metadata->key + TKIP_TX_MIC_KEY_OFFSET, data->destination->bytes, data->source->bytes,
		data->priority, data->data, data->data_size, mic);

	u80211_memcpy(metadata->tx_sequence, sequence, sizeof(sequence));
	u80211_memcpy(data->sequence, sequence, sizeof(sequence));
	u80211_memcpy(data->mic, mic, sizeof(mic));
	valid = true;

unlock:
	u80211_kernel_release_spinlock(device->key_spinlock);
	return valid;
}

bool u80211_key_next_tx_sequence(u80211_device_t *device, const u80211_header_description_t *header, uint8_t index, u80211_cipher_t cipher, uint8_t *sequence, size_t sequence_size) {
	if (sequence_size != SEQUENCE_SIZE)
		return false;

	u80211_kernel_acquire_spinlock(device->key_spinlock);
	u80211_key_metadata_t *metadata = select_key(device, header);
	bool valid = false;
	if (metadata == NULL || metadata->index != index || metadata->cipher != cipher)
		goto unlock;
	if (!increment_sequence(metadata->tx_sequence, sequence_size))
		goto unlock;

	u80211_memcpy(sequence, metadata->tx_sequence, sequence_size);
	valid = true;

unlock:
	u80211_kernel_release_spinlock(device->key_spinlock);
	return valid;
}
