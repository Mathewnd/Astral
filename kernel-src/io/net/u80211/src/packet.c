#include <u80211/association.h>
#include <u80211/packet.h>
#include <u80211/status.h>
#include <u80211/string.h>
#include <u80211/u80211.h>
#include <u80211/util.h>

#define CCMP_HEADER_SIZE 8
#define CCMP_MIC_SIZE 8
#define CCMP_EXT_IV 0x20
#define TKIP_HEADER_SIZE 8
#define TKIP_MIC_SIZE 8
#define TKIP_ICV_SIZE 4
#define TKIP_EXT_IV 0x20
#define SEQUENCE_SIZE 6

static bool deserialize_cipher_key_index(const void *data, size_t data_size, uint8_t *index) {
	// all currently supported 802.11 cipher headers store the key id in bits 6-7 of byte 3
	if (data_size < 4)
		return false;

	*index = ((const uint8_t *)data)[3] >> 6;
	return true;
}

static bool data_packet_for_device(u80211_device_t *device, const u80211_header_description_t *header) {
	return u80211_mac_address_equal(&header->addresses[0], &device->metadata.mac_address) || (header->addresses[0].bytes[0] & 1);
}

static void data_packet_addresses(const u80211_header_description_t *header, const u80211_mac_address_t **destination, const u80211_mac_address_t **source) {
	bool to_ds = header->frame_control & U80211_HEADER_FRAME_CONTROL_TO_DS;
	bool from_ds = header->frame_control & U80211_HEADER_FRAME_CONTROL_FROM_DS;

	if (to_ds) {
		*destination = &header->addresses[2];
		*source = from_ds ? &header->addresses[3] : &header->addresses[1];
	} else {
		*destination = &header->addresses[0];
		*source = from_ds ? &header->addresses[2] : &header->addresses[1];
	}
}

static bool process_ccmp_rx(u80211_device_t *device, const u80211_header_description_t *header, uint8_t key_index, void **data_start, size_t *data_size) {
	if (*data_size < CCMP_HEADER_SIZE + CCMP_MIC_SIZE)
		return false;

	const uint8_t *ccmp_header = *data_start;
	if (!(ccmp_header[3] & CCMP_EXT_IV))
		return false;

	const uint8_t sequence[SEQUENCE_SIZE] = {
		ccmp_header[0], ccmp_header[1], ccmp_header[4],
		ccmp_header[5], ccmp_header[6], ccmp_header[7],
	};

	if (!u80211_key_update_rx_sequence(device, header, key_index, U80211_CIPHER_CCMP, sequence, sizeof(sequence)))
		return false;

	*data_start = (uint8_t *)*data_start + CCMP_HEADER_SIZE;
	*data_size -= CCMP_HEADER_SIZE + CCMP_MIC_SIZE;
	return true;
}

static bool process_tkip_rx(u80211_device_t *device, const u80211_header_description_t *header, uint8_t key_index, void **data_start, size_t *data_size) {
	// TODO: support qos data frames and use their tid as the michael priority
	if (U80211_HEADER_FRAME_CONTROL_GET_TYPE(header->frame_control) != U80211_HEADER_FRAME_CONTROL_TYPE_DATA)
		return false;
	
	if (U80211_HEADER_FRAME_CONTROL_GET_SUBTYPE(header->frame_control) != U80211_HEADER_FRAME_CONTROL_SUBTYPE_DATA)
		return false;

	if (*data_size < TKIP_HEADER_SIZE + TKIP_MIC_SIZE + TKIP_ICV_SIZE)
		return false;

	const uint8_t *tkip_header = *data_start;
	if (!(tkip_header[3] & TKIP_EXT_IV))
		return false;
	if (tkip_header[1] != ((tkip_header[0] | 0x20) & 0x7f))
		return false;

	const uint8_t *payload = tkip_header + TKIP_HEADER_SIZE;
	size_t payload_size = *data_size - TKIP_HEADER_SIZE - TKIP_MIC_SIZE - TKIP_ICV_SIZE;
	const uint8_t *mic = payload + payload_size;

	u80211_tkip_data_t tkip_data = {
		.header = header,
		.key_index = key_index,
		.priority = 0,
		.data = payload,
		.data_size = payload_size,
		.sequence = {
			tkip_header[2], tkip_header[0], tkip_header[4],
			tkip_header[5], tkip_header[6], tkip_header[7],
		},
	};
	u80211_memcpy(tkip_data.mic, mic, sizeof(tkip_data.mic));
	data_packet_addresses(header, &tkip_data.destination, &tkip_data.source);

	if (!u80211_key_validate_tkip_rx(device, &tkip_data))
		return false;

	*data_start = (void *)payload;
	*data_size = payload_size;
	return true;
}

void u80211_process_packet(u80211_device_t *device, void *packet, size_t packet_size) {
	__atomic_add_fetch(&device->packet_count, 1, __ATOMIC_RELAXED);

	void *data_start;
	size_t data_size;
	u80211_header_description_t header;
	if (u80211_deserialize_header(packet, packet_size, &header, &data_start, &data_size) != U80211_STATUS_SUCCESS)
		return;

	// encrypted packets contain an additional header that we need to handle
	// TODO: this might be handled by future hardware, so this needs to be a flag in the device metadata
	if (header.frame_control & U80211_HEADER_FRAME_CONTROL_PROTECTED_FRAME) {
		uint8_t key_index;
		if (!deserialize_cipher_key_index(data_start, data_size, &key_index))
			return;

		switch (u80211_select_cipher_by_index(device, &header, key_index)) {
			case U80211_CIPHER_CCMP:
				if (!process_ccmp_rx(device, &header, key_index, &data_start, &data_size))
					return;
				break;
			case U80211_CIPHER_TKIP:
				if (!process_tkip_rx(device, &header, key_index, &data_start, &data_size))
					return;
				break;
			default:
				return;
		}
	}

	switch (U80211_HEADER_FRAME_CONTROL_GET_TYPE(header.frame_control)) {
		case U80211_HEADER_FRAME_CONTROL_TYPE_MANAGEMENT:
			u80211_process_management_packet(device, &header, data_start, data_size);
			break;
		case U80211_HEADER_FRAME_CONTROL_TYPE_DATA:
			if (data_packet_for_device(device, &header) &&
				u80211_association_is_duplicate(device, &header, U80211_DEVICE_STATE_ASSOCIATED))
				return;
			u80211_process_data_packet(device, &header, data_start, data_size);
			break;
	}
}

size_t u80211_get_packet_count(u80211_device_t *device) {
	return __atomic_load_n(&device->packet_count, __ATOMIC_RELAXED);
}

void u80211_reset_packet_count(u80211_device_t *device) {
	__atomic_store_n(&device->packet_count, 0, __ATOMIC_RELAXED);
}

static int deserialize_management_header(void *source, size_t source_size, u80211_header_description_t *header, void **data_start, size_t *data_size) {
	if (source_size < 14)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	u80211_memcpy(&header->addresses[1], source, 6);
	u80211_memcpy(&header->addresses[2], (const void *)((uintptr_t)source + 6), 6);
	header->sequence_control = deserialize_le16((const void *)((uintptr_t)source + 12));

	*data_start = (void *)((uintptr_t)source + 14);
	*data_size = source_size - 14;
	return U80211_STATUS_SUCCESS;
}

static int deserialize_control_header(void *source, size_t source_size, u80211_header_description_t *header, void **data_start, size_t *data_size) {
	(void)source;
	(void)source_size;
	(void)header;
	(void)data_start;
	(void)data_size;
	return U80211_STATUS_UNSUPPORTED;
}

static int deserialize_data_header(void *source, size_t source_size, u80211_header_description_t *header, void **data_start, size_t *data_size) {
	int subtype = U80211_HEADER_FRAME_CONTROL_GET_SUBTYPE(header->frame_control);

	if (subtype != U80211_HEADER_FRAME_CONTROL_SUBTYPE_DATA && subtype != U80211_HEADER_FRAME_CONTROL_SUBTYPE_NULL_DATA)
		return U80211_STATUS_UNSUPPORTED;

	size_t header_size = (header->frame_control & U80211_HEADER_FRAME_CONTROL_TO_DS) && (header->frame_control & U80211_HEADER_FRAME_CONTROL_FROM_DS) ? 20 : 14;
	if (source_size < header_size)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	u80211_memcpy(&header->addresses[1], source, 6);
	u80211_memcpy(&header->addresses[2], (const void *)((uintptr_t)source + 6), 6);

	if (header_size == 20) {
		header->sequence_control = deserialize_le16((const void *)((uintptr_t)source + 12));
		u80211_memcpy(&header->addresses[3], (const void *)((uintptr_t)source + 14), 6);
	} else {
		header->sequence_control = deserialize_le16((const void *)((uintptr_t)source + 12));
	}

	*data_start = (void *)((uintptr_t)source + header_size);
	*data_size = source_size - header_size;
	return U80211_STATUS_SUCCESS;
}

int u80211_deserialize_header(void *source, size_t source_size, u80211_header_description_t *header, void **data_start, size_t *data_size) {
	if (source_size < 10)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	header->frame_control = deserialize_le16(source);
	header->duration_id = deserialize_le16((const void *)((uintptr_t)source + 2));
	u80211_memcpy(&header->addresses[0], (const void *)((uintptr_t)source + 4), 6);

	void *next_part = (void *)((uintptr_t)source + 10);
	size_t next_part_size = source_size - 10;

	switch (U80211_HEADER_FRAME_CONTROL_GET_TYPE(header->frame_control)) {
		case U80211_HEADER_FRAME_CONTROL_TYPE_MANAGEMENT:
			return deserialize_management_header(next_part, next_part_size, header, data_start, data_size);
		case U80211_HEADER_FRAME_CONTROL_TYPE_CONTROL:
			return deserialize_control_header(next_part, next_part_size, header, data_start, data_size);
		case U80211_HEADER_FRAME_CONTROL_TYPE_DATA:
			return deserialize_data_header(next_part, next_part_size, header, data_start, data_size);
		default:
			return U80211_STATUS_UNSUPPORTED;
	}
}


static void serialize_common_header(u80211_header_description_t *header, uint8_t *destination) {
	serialize_le16(destination, header->frame_control);
	serialize_le16(destination + 2, header->duration_id);
	u80211_memcpy(destination + 4, &header->addresses[0], 6);
}

static int serialize_management_header(u80211_header_description_t *header, u80211_tx_buffer_descriptor_t *descriptor) {
	uint8_t *destination = u80211_descriptor_allocate_space(descriptor, 24);
	if (destination == NULL)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	serialize_common_header(header, destination);
	u80211_memcpy(destination + 10, &header->addresses[1], 6);
	u80211_memcpy(destination + 16, &header->addresses[2], 6);
	serialize_le16(destination + 22, header->sequence_control);

	return U80211_STATUS_SUCCESS;
}

static int serialize_control_header(u80211_header_description_t *header, u80211_tx_buffer_descriptor_t *descriptor) {
	(void)header;
	(void)descriptor;
	return U80211_STATUS_UNSUPPORTED;
}

static int serialize_data_header(u80211_header_description_t *header, u80211_tx_buffer_descriptor_t *descriptor) {
	int subtype = U80211_HEADER_FRAME_CONTROL_GET_SUBTYPE(header->frame_control);

	if (subtype != U80211_HEADER_FRAME_CONTROL_SUBTYPE_DATA && subtype != U80211_HEADER_FRAME_CONTROL_SUBTYPE_NULL_DATA)
		return U80211_STATUS_UNSUPPORTED;

	size_t header_size = (header->frame_control & U80211_HEADER_FRAME_CONTROL_TO_DS) && (header->frame_control & U80211_HEADER_FRAME_CONTROL_FROM_DS) ? 30 : 24;

	uint8_t *destination = u80211_descriptor_allocate_space(descriptor, header_size);
	if (destination == NULL)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	serialize_common_header(header, destination);
	u80211_memcpy(destination + 10, &header->addresses[1], 6);
	u80211_memcpy(destination + 16, &header->addresses[2], 6);

	if (header_size == 30) {
		serialize_le16(destination + 22, header->sequence_control);
		u80211_memcpy(destination + 24, &header->addresses[3], 6);
	} else {
		serialize_le16(destination + 22, header->sequence_control);
	}

	return U80211_STATUS_SUCCESS;
}

int u80211_serialize_header(u80211_header_description_t *header, u80211_tx_buffer_descriptor_t *descriptor) {
	switch (U80211_HEADER_FRAME_CONTROL_GET_TYPE(header->frame_control)) {
		case U80211_HEADER_FRAME_CONTROL_TYPE_MANAGEMENT:
			return serialize_management_header(header, descriptor);
		case U80211_HEADER_FRAME_CONTROL_TYPE_CONTROL:
			return serialize_control_header(header, descriptor);
		case U80211_HEADER_FRAME_CONTROL_TYPE_DATA:
			return serialize_data_header(header, descriptor);
		default:
			return U80211_STATUS_UNSUPPORTED;
	}
}
