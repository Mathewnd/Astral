#include <u80211/packet.h>
#include <u80211/string.h>
#include <u80211/kernel_interface.h>
#include <u80211/status.h>

#define ETHERNET_HEADER_SIZE 14
#define ETHERNET_MAX_FRAME_SIZE 1514
#define LLCSNAP_SIZE 8
#define TX_BUFFER_HEADROOM 64
#define CCMP_HEADER_SIZE 8
#define CCMP_EXT_IV 0x20
#define TKIP_HEADER_SIZE 8
#define TKIP_MIC_SIZE 8
#define TKIP_EXT_IV 0x20
#define TX_BUFFER_TAILROOM TKIP_MIC_SIZE
#define SEQUENCE_SIZE 6

// this is the expected LLC/SNAP header for our use-case. it is then followed by a big-endian 16-bit ethertype.
const uint8_t byte_header[6] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};

void u80211_process_data_packet(u80211_device_t *device, u80211_header_description_t *header, void *data, size_t data_size) {
	if (data_size < LLCSNAP_SIZE)
		return;

	if (u80211_memcmp(data, byte_header, sizeof(byte_header)) != 0)
		return;

	uint16_t ethertype;
	u80211_memcpy(&ethertype, (void *)((uintptr_t)data + 6), sizeof(ethertype));

	// create an ethernet header before the payload
	// doing this in-place is safe due to the LLC/SNAP header + 802.11 header being much 
	// larger than the ethernet header.
	data = (void *)((uintptr_t)data + LLCSNAP_SIZE - ETHERNET_HEADER_SIZE);
	data_size = data_size + ETHERNET_HEADER_SIZE - LLCSNAP_SIZE;

	u80211_mac_address_t *destination;
	u80211_mac_address_t *source;
	bool to_ds = header->frame_control & U80211_HEADER_FRAME_CONTROL_TO_DS;
	bool from_ds = header->frame_control & U80211_HEADER_FRAME_CONTROL_FROM_DS;

	if (to_ds) {
		destination = &header->addresses[2];
		source = from_ds ? &header->addresses[3] : &header->addresses[1];
	} else {
		destination = &header->addresses[0];
		source = from_ds ? &header->addresses[2] : &header->addresses[1];
	}

	u80211_memcpy(data, destination, sizeof(*destination));
	u80211_memcpy((void *)((uintptr_t)data + 6), source, sizeof(*source));
	u80211_memcpy((void *)((uintptr_t)data + 12), &ethertype, sizeof(ethertype));

	u80211_kernel_receive_callback(device, data, data_size);
}

static int prepare_ccmp_tx(u80211_device_t *device, u80211_header_description_t *header, uint8_t key, u80211_tx_buffer_descriptor_t *descriptor) {
	uint8_t *ccmp_header = u80211_descriptor_allocate_space(descriptor, CCMP_HEADER_SIZE);
	if (ccmp_header == NULL)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	uint8_t sequence[SEQUENCE_SIZE];
	if (!u80211_key_next_tx_sequence(device, header, key, U80211_CIPHER_CCMP, sequence, sizeof(sequence)))
		return U80211_STATUS_NOT_PERMITTED;

	// TODO: other hardware might provide more complete cipher handling, including the CCMP header
	ccmp_header[0] = sequence[0];
	ccmp_header[1] = sequence[1];
	ccmp_header[2] = 0;
	ccmp_header[3] = CCMP_EXT_IV | key << 6;
	ccmp_header[4] = sequence[2];
	ccmp_header[5] = sequence[3];
	ccmp_header[6] = sequence[4];
	ccmp_header[7] = sequence[5];
	header->frame_control |= U80211_HEADER_FRAME_CONTROL_PROTECTED_FRAME;
	return U80211_STATUS_SUCCESS;
}

static int prepare_tkip_tx(u80211_device_t *device, u80211_header_description_t *header, uint8_t key, u80211_tx_buffer_descriptor_t *descriptor) {
	u80211_tkip_data_t tkip_data = {
		.header = header,
		.key_index = key,
		.destination = &header->addresses[2],
		.source = &header->addresses[1],
		.priority = 0,
		.data = (uint8_t *)descriptor->data + descriptor->current_offset,
		.data_size = descriptor->size - descriptor->current_offset,
	};

	uint8_t *tkip_header = u80211_descriptor_allocate_space(descriptor, TKIP_HEADER_SIZE);
	if (tkip_header == NULL)
		return U80211_STATUS_NOT_ENOUGH_SPACE;

	if (!u80211_key_prepare_tkip_tx(device, &tkip_data))
		return U80211_STATUS_NOT_PERMITTED;

	// TODO: other hardware might not expect something exactly like this. this code adds both
	// the header and the trailing mic.
	tkip_header[0] = tkip_data.sequence[1];
	tkip_header[1] = (tkip_data.sequence[1] | 0x20) & 0x7f;
	tkip_header[2] = tkip_data.sequence[0];
	tkip_header[3] = TKIP_EXT_IV | key << 6;
	tkip_header[4] = tkip_data.sequence[2];
	tkip_header[5] = tkip_data.sequence[3];
	tkip_header[6] = tkip_data.sequence[4];
	tkip_header[7] = tkip_data.sequence[5];
	u80211_memcpy((uint8_t *)descriptor->data + descriptor->size, tkip_data.mic, TKIP_MIC_SIZE);
	descriptor->size += TKIP_MIC_SIZE;
	header->frame_control |= U80211_HEADER_FRAME_CONTROL_PROTECTED_FRAME;
	return U80211_STATUS_SUCCESS;
}

int u80211_allocate_tx_buffer(u80211_device_t *device, u80211_tx_buffer_descriptor_t *descriptor) {
	int status = device->ops->allocate_tx_buffer(device, ETHERNET_MAX_FRAME_SIZE + TX_BUFFER_HEADROOM + TX_BUFFER_TAILROOM, descriptor);
	if (status != U80211_STATUS_SUCCESS)
		return status;

	// keep MIC tailroom outside the logical buffer so other ciphers do not transmit it
	descriptor->data = (void *)((uintptr_t)descriptor->data + TX_BUFFER_HEADROOM);
	descriptor->size -= TX_BUFFER_HEADROOM + TX_BUFFER_TAILROOM;
	descriptor->current_offset -= TX_BUFFER_HEADROOM + TX_BUFFER_TAILROOM;
	return U80211_STATUS_SUCCESS;
}

int u80211_transmit_buffer(u80211_device_t *device, u80211_tx_buffer_descriptor_t *descriptor) {
	descriptor->data = (uint8_t *)descriptor->data - TX_BUFFER_HEADROOM;
	descriptor->size += TX_BUFFER_HEADROOM;
	descriptor->current_offset += TX_BUFFER_HEADROOM;

	u80211_kernel_acquire_spinlock(device->association_spinlock);
	u80211_ap_t *ap = device->ap;
	if (u80211_get_device_state(device) != U80211_DEVICE_STATE_ASSOCIATED || ap == NULL) {
		u80211_kernel_release_spinlock(device->association_spinlock);
		device->ops->free_tx_buffer(device, descriptor);
		return U80211_STATUS_NOT_ASSOCIATED;
	}
	u80211_ap_hold(ap);
	u80211_kernel_release_spinlock(device->association_spinlock);

	size_t ethernet_frame_size = descriptor->size - descriptor->current_offset;
	if (ethernet_frame_size < ETHERNET_HEADER_SIZE) {
		u80211_ap_release(ap);
		device->ops->free_tx_buffer(device, descriptor);
		return U80211_STATUS_NOT_ENOUGH_SPACE;
	}

	uint8_t *ethernet_header = (uint8_t *)descriptor->data + descriptor->current_offset;
	u80211_mac_address_t destination;
	u80211_mac_address_t source;
	uint16_t ethertype;
	u80211_memcpy(&destination, ethernet_header, sizeof(destination));
	u80211_memcpy(&source, ethernet_header + 6, sizeof(source));
	u80211_memcpy(&ethertype, ethernet_header + 12, sizeof(ethertype));

	descriptor->current_offset += ETHERNET_HEADER_SIZE;
	uint8_t *llc_snap = u80211_descriptor_allocate_space(descriptor, LLCSNAP_SIZE);
	if (llc_snap == NULL) {
		u80211_ap_release(ap);
		device->ops->free_tx_buffer(device, descriptor);
		return U80211_STATUS_NOT_ENOUGH_SPACE;
	}
	u80211_memcpy(llc_snap, byte_header, sizeof(byte_header));
	u80211_memcpy(llc_snap + sizeof(byte_header), &ethertype, sizeof(ethertype));

	u80211_header_description_t header = {
		.frame_control = (U80211_HEADER_FRAME_CONTROL_TYPE_DATA << 2) | U80211_HEADER_FRAME_CONTROL_TO_DS,
		.sequence_control = __atomic_fetch_add(&device->tx_sequence_control, 0x10, __ATOMIC_RELAXED),
		.addresses = {
			ap->mac_address,
			source,
			destination,
		},
	};
	u80211_ap_release(ap);

	int key = u80211_select_key(device, &header);
	int cipher = -1;
	if (key >= 0) {
		// supported cipher headers have two bits for key selection
		if (key > 3) {
			device->ops->free_tx_buffer(device, descriptor);
			return U80211_STATUS_NOT_PERMITTED;
		}

		cipher = u80211_select_cipher_by_index(device, &header, key);
		int cipher_status;
		switch (cipher) {
			case U80211_CIPHER_CCMP:
				cipher_status = prepare_ccmp_tx(device, &header, key, descriptor);
				break;
			case U80211_CIPHER_TKIP:
				cipher_status = prepare_tkip_tx(device, &header, key, descriptor);
				break;
			default:
				device->ops->free_tx_buffer(device, descriptor);
				return U80211_STATUS_UNSUPPORTED;
		}
		if (cipher_status != U80211_STATUS_SUCCESS) {
			device->ops->free_tx_buffer(device, descriptor);
			return cipher_status;
		}
	}

	int status = u80211_serialize_header(&header, descriptor);
	if (status != U80211_STATUS_SUCCESS) {
		device->ops->free_tx_buffer(device, descriptor);
		return status;
	}

	const u80211_transmit_options_t options = { .key = key, .cipher = cipher };
	return device->ops->transmit(device, descriptor, &options);
}
