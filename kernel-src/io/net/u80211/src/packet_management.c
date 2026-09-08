#include <stdbool.h>

#include <u80211/packet.h>
#include <u80211/scan.h>
#include <u80211/association.h>
#include <u80211/status.h>
#include <u80211/string.h>
#include <u80211/u80211.h>
#include <u80211/util.h>

#define IE_ID_SSID 0
#define IE_ID_RATES 1
#define IE_ID_CHANNEL 3
#define IE_ID_RSN 48
#define IE_ID_RATES_EXT 50

#define MANAGEMENT_HEADER_SIZE 24
#define AUTH_DATA_SIZE 6
#define REASON_CODE_SIZE 2
#define ASSOCIATION_REQUEST_FIXED_SIZE 4
#define ASSOCIATION_RESPONSE_FIXED_SIZE 6
#define CAPABILITY_ESS 0x0001
#define CAPABILITY_SHORT_PREAMBLE 0x0020
#define CAPABILITY_SHORT_SLOT_TIME 0x0400
// TODO: Only hardcode these capabilities on 2.4 GHz.
#define ASSOCIATION_CAPABILITIES (CAPABILITY_ESS | CAPABILITY_SHORT_PREAMBLE | CAPABILITY_SHORT_SLOT_TIME)
// TODO: Revisit the listen interval when power saving is implemented.
#define ASSOCIATION_LISTEN_INTERVAL 10
#define MAX_SSID_SIZE 32
#define MAX_RATE_VALUE 125

// TODO check if basic rates are supported by the device
static bool handle_rates(uint8_t rate_bitmap[16], const uint8_t *rates, size_t count) {
	for (size_t i = 0; i < count; ++i) {
		uint8_t rate = rates[i] & 0x7f;

		// these two have special meanings, so ignore them
		if (rate == 126 || rate == 127)
			continue;

		rate_bitmap[rate / 8] |= 1 << (rate % 8);
	}

	return true;
}

static bool management_packet_for_device(u80211_device_t *device, u80211_header_description_t *header) {
	return u80211_mac_address_equal(&header->addresses[1], &header->addresses[2]) && u80211_mac_address_equal(&header->addresses[0], &device->metadata.mac_address);
}

static bool management_packet_for_device_or_broadcast(u80211_device_t *device, u80211_header_description_t *header) {
	const u80211_mac_address_t broadcast_address = { .bytes = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } };

	return u80211_mac_address_equal(&header->addresses[1], &header->addresses[2]) &&
		(u80211_mac_address_equal(&header->addresses[0], &device->metadata.mac_address) ||
		 u80211_mac_address_equal(&header->addresses[0], &broadcast_address));
}

static bool process_information_elements(uint8_t rate_bitmap[16], char *ssid, uint8_t *channel, const uint8_t **rsn, size_t *rsn_size, const void *data, size_t data_size) {
	const uint8_t *bytes = data;
	size_t offset = 0;
	while (offset < data_size) {
		if (data_size - offset < 2)
			return false;

		uint8_t id = bytes[offset];
		uint8_t size = bytes[offset + 1];
		offset += 2;

		if (data_size - offset < size)
			return false;

		switch (id) {
			case IE_ID_SSID:
				if (size > MAX_SSID_SIZE)
					return false;

				if (ssid != NULL) {
					u80211_memcpy(ssid, bytes + offset, size);
					ssid[size] = '\0';
				}
				break;
			case IE_ID_CHANNEL:
				if (size != 1)
					return false;

				if (channel != NULL)
					*channel = bytes[offset];
				break;
			case IE_ID_RATES:
				if (size > 8)
					return false;
				/* fall through */
			case IE_ID_RATES_EXT:
				if (!handle_rates(rate_bitmap, bytes + offset, size))
					return false;
				break;
			case IE_ID_RSN:
				if (rsn != NULL && rsn_size != NULL) {
					*rsn = bytes + offset;
					*rsn_size = size;
				}
				break;
		}

		offset += size;
	}

	return true;
}

static size_t count_rates(const uint8_t rate_bitmap[16]) {
	size_t rate_count = 0;
	for (size_t rate = 0; rate <= MAX_RATE_VALUE; ++rate) {
		if (rate_bitmap[rate / 8] & (1 << (rate % 8)))
			++rate_count;
	}

	return rate_count;
}

static size_t rate_information_elements_size(size_t rate_count) {
	size_t size = 2 + min(rate_count, 8);

	if (rate_count > 8)
		size += 2 + rate_count - 8;

	return size;
}

static size_t serialize_rate_information_elements(uint8_t *destination, const uint8_t rate_bitmap[16], size_t rate_count) {
	size_t supported_rate_count = min(rate_count, 8);
	size_t extended_rate_count = rate_count - supported_rate_count;
	size_t offset = 0;

	destination[offset++] = IE_ID_RATES;
	destination[offset++] = supported_rate_count;

	size_t rate_index = 0;
	for (size_t rate = 0; rate <= MAX_RATE_VALUE; ++rate) {
		if (!(rate_bitmap[rate / 8] & (1 << (rate % 8))))
			continue;

		if (rate_index == supported_rate_count && extended_rate_count != 0) {
			destination[offset++] = IE_ID_RATES_EXT;
			destination[offset++] = extended_rate_count;
		}

		destination[offset++] = rate;
		++rate_index;
	}

	return offset;
}

static int prepare_management_packet(u80211_device_t *device, const u80211_mac_address_t *address, int subtype, size_t data_size, u80211_tx_buffer_descriptor_t *descriptor, uint8_t **data) {
	int status = device->ops->allocate_tx_buffer(device, MANAGEMENT_HEADER_SIZE + data_size, descriptor);
	if (status != U80211_STATUS_SUCCESS)
		return status;

	*data = u80211_descriptor_allocate_space(descriptor, data_size);
	if (*data == NULL) {
		device->ops->free_tx_buffer(device, descriptor);
		return U80211_STATUS_NOT_ENOUGH_SPACE;
	}

	u80211_header_description_t header = {
		.frame_control = subtype << 4,
		.sequence_control = __atomic_fetch_add(&device->tx_sequence_control, 0x10, __ATOMIC_RELAXED),
		.addresses = {
			*address,
			device->metadata.mac_address,
			*address,
		},
	};

	status = u80211_serialize_header(&header, descriptor);
	if (status != U80211_STATUS_SUCCESS)
		device->ops->free_tx_buffer(device, descriptor);

	return status;
}

static void process_probe_response(u80211_device_t *device, u80211_header_description_t *header, const void *data, size_t data_size) {
	if (data_size < 12)
		return;

	if (!management_packet_for_device(device, header))
		return;

	u80211_beacon_data_t beacon_data;
	u80211_memset(&beacon_data, 0, sizeof(beacon_data));
	beacon_data.interval = deserialize_le16((const void *)((uintptr_t)data + 8));
	beacon_data.capabilities = deserialize_le16((const void *)((uintptr_t)data + 10));
	beacon_data.mac_address = header->addresses[2];

	if (!process_information_elements(beacon_data.rate_bitmap, beacon_data.ssid, &beacon_data.channel, &beacon_data.rsn, &beacon_data.rsn_size, (const void *)((uintptr_t)data + 12), data_size - 12))
		return;

	u80211_scan_process_response(device, &beacon_data);
}

static void process_auth_packet(u80211_device_t *device, u80211_header_description_t *header, const void *data, size_t data_size) {
	if (data_size < 6)
		return;

	if (!management_packet_for_device(device, header))
		return;

	u80211_auth_data_t auth_data;
	auth_data.address = header->addresses[2];
	auth_data.auth_algorithm = deserialize_le16(data);
	auth_data.auth_transaction = deserialize_le16((const void *)((uintptr_t)data + 2));
	auth_data.status = deserialize_le16((const void *)((uintptr_t)data + 4));

	if (u80211_association_is_duplicate(device, header, U80211_DEVICE_STATE_AUTHENTICATING))
		return;

	u80211_association_process_authentication(device, &auth_data);
}

static void process_association_response(u80211_device_t *device, u80211_header_description_t *header, const void *data, size_t data_size) {
	if (data_size < ASSOCIATION_RESPONSE_FIXED_SIZE)
		return;

	if (!management_packet_for_device(device, header))
		return;

	u80211_association_response_data_t association_data;
	u80211_memset(&association_data, 0, sizeof(association_data));
	association_data.address = header->addresses[2];
	association_data.capabilities = deserialize_le16(data);
	association_data.status = deserialize_le16((const void *)((uintptr_t)data + 2));
	association_data.association_id = deserialize_le16((const void *)((uintptr_t)data + 4));

	if (!process_information_elements(association_data.rate_bitmap, NULL, NULL, NULL, NULL, (const void *)((uintptr_t)data + ASSOCIATION_RESPONSE_FIXED_SIZE), data_size - ASSOCIATION_RESPONSE_FIXED_SIZE))
		return;

	if (u80211_association_is_duplicate(device, header, U80211_DEVICE_STATE_ASSOCIATING))
		return;

	u80211_association_process_response(device, &association_data);
}

static void process_deauthentication_packet(u80211_device_t *device, u80211_header_description_t *header, const void *data, size_t data_size) {
	if (data_size < REASON_CODE_SIZE)
		return;

	if (!management_packet_for_device_or_broadcast(device, header))
		return;

	u80211_deauthentication_data_t deauthentication_data = {
		.address = header->addresses[2],
		.reason = deserialize_le16(data),
	};
	u80211_association_process_deauthentication(device, &deauthentication_data);
}

static void process_disassociation_packet(u80211_device_t *device, u80211_header_description_t *header, const void *data, size_t data_size) {
	if (data_size < REASON_CODE_SIZE)
		return;

	if (!management_packet_for_device_or_broadcast(device, header))
		return;

	u80211_disassociation_data_t disassociation_data = {
		.address = header->addresses[2],
		.reason = deserialize_le16(data),
	};
	u80211_association_process_disassociation(device, &disassociation_data);
}

void u80211_process_management_packet(u80211_device_t *device, u80211_header_description_t *header, const void *data, size_t data_size) {
	int subtype = U80211_HEADER_FRAME_CONTROL_GET_SUBTYPE(header->frame_control);

	switch (subtype) {
		case U80211_HEADER_FRAME_CONTROL_SUBTYPE_ASSOCIATION_RESPONSE:
			process_association_response(device, header, data, data_size);
			break;
		case U80211_HEADER_FRAME_CONTROL_SUBTYPE_PROBE_RESPONSE:
			process_probe_response(device, header, data, data_size);
			break;
		case U80211_HEADER_FRAME_CONTROL_SUBTYPE_AUTHENTICATION:
			process_auth_packet(device, header, data, data_size);
			break;
		case U80211_HEADER_FRAME_CONTROL_SUBTYPE_DEAUTHENTICATION:
			process_deauthentication_packet(device, header, data, data_size);
			break;
		case U80211_HEADER_FRAME_CONTROL_SUBTYPE_DISASSOCIATION:
			process_disassociation_packet(device, header, data, data_size);
			break;
	}
}

int u80211_send_authentication(u80211_device_t *device, u80211_auth_data_t *auth_data) {
	u80211_tx_buffer_descriptor_t descriptor;
	uint8_t *data;
	int status = prepare_management_packet(device, &auth_data->address, U80211_HEADER_FRAME_CONTROL_SUBTYPE_AUTHENTICATION, AUTH_DATA_SIZE, &descriptor, &data);
	if (status != U80211_STATUS_SUCCESS)
		return status;

	serialize_le16(data, auth_data->auth_algorithm);
	serialize_le16(data + 2, auth_data->auth_transaction);
	serialize_le16(data + 4, auth_data->status);

	const u80211_transmit_options_t options = { .key = -1, .cipher = -1 };
	return device->ops->transmit(device, &descriptor, &options);
}

int u80211_send_deauthentication(u80211_device_t *device, u80211_deauthentication_data_t *deauthentication_data) {
	u80211_tx_buffer_descriptor_t descriptor;
	uint8_t *data;
	int status = prepare_management_packet(device, &deauthentication_data->address, U80211_HEADER_FRAME_CONTROL_SUBTYPE_DEAUTHENTICATION, REASON_CODE_SIZE, &descriptor, &data);
	if (status != U80211_STATUS_SUCCESS)
		return status;

	serialize_le16(data, deauthentication_data->reason);

	const u80211_transmit_options_t options = { .key = -1, .cipher = -1 };
	return device->ops->transmit(device, &descriptor, &options);
}

int u80211_send_probe_request(u80211_device_t *device) {
	size_t rate_count = count_rates(device->metadata.rate_bitmap);
	size_t probe_request_data_size = 2 + rate_information_elements_size(rate_count);
	const u80211_mac_address_t broadcast_address = { .bytes = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } };

	u80211_tx_buffer_descriptor_t descriptor;
	uint8_t *probe_request_data;
	int status = prepare_management_packet(device, &broadcast_address, U80211_HEADER_FRAME_CONTROL_SUBTYPE_PROBE_REQUEST, probe_request_data_size, &descriptor, &probe_request_data);
	if (status != U80211_STATUS_SUCCESS)
		return status;

	probe_request_data[0] = IE_ID_SSID;
	probe_request_data[1] = 0;
	serialize_rate_information_elements(probe_request_data + 2, device->metadata.rate_bitmap, rate_count);

	const u80211_transmit_options_t options = { .key = -1, .cipher = -1 };
	return device->ops->transmit(device, &descriptor, &options);
}

int u80211_send_association_request(u80211_device_t *device, const void *information_elements, size_t information_elements_size) {
	u80211_ap_t *ap = device->ap;
	size_t ssid_size = 0;
	while (ssid_size < MAX_SSID_SIZE && ap->ssid[ssid_size] != '\0')
		++ssid_size;

	size_t rate_count = count_rates(device->metadata.rate_bitmap);
	size_t generated_data_size = ASSOCIATION_REQUEST_FIXED_SIZE + 2 + ssid_size + rate_information_elements_size(rate_count);
	size_t association_request_data_size = generated_data_size + information_elements_size;

	u80211_tx_buffer_descriptor_t descriptor;
	uint8_t *data;
	int status = prepare_management_packet(device, &ap->mac_address, U80211_HEADER_FRAME_CONTROL_SUBTYPE_ASSOCIATION_REQUEST, association_request_data_size, &descriptor, &data);
	if (status != U80211_STATUS_SUCCESS)
		return status;

	serialize_le16(data, ASSOCIATION_CAPABILITIES);
	serialize_le16(data + 2, ASSOCIATION_LISTEN_INTERVAL);
	data[4] = IE_ID_SSID;
	data[5] = ssid_size;
	u80211_memcpy(data + 6, ap->ssid, ssid_size);
	size_t rate_information_size = serialize_rate_information_elements(data + 6 + ssid_size, device->metadata.rate_bitmap, rate_count);
	if (information_elements_size != 0)
		u80211_memcpy(data + 6 + ssid_size + rate_information_size, information_elements, information_elements_size);

	const u80211_transmit_options_t options = { .key = -1, .cipher = -1 };
	return device->ops->transmit(device, &descriptor, &options);
}
