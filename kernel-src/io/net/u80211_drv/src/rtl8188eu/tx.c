#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <u80211_drv/80211.h>
#include <u80211_drv/kernel_interface.h>
#include <u80211_drv/rtl8188eu.h>
#include <u80211_drv/status.h>
#include <u80211_drv/string.h>
#include <u80211_drv/util.h>

#define RTL8188EU_TX_DESCRIPTOR_SIZE 32
#define RTL8188EU_TX_TIMEOUT_MS 1000

#define RTL8188EU_TX_QUEUE_BEST_EFFORT 0x00
#define RTL8188EU_TX_QUEUE_MANAGEMENT 0x12
#define RTL8188EU_TX_RAID_11BG 4
#define RTL8188EU_TX_RAID_11B 6
#define RTL8188EU_TX_RAID_SHIFT 16
#define RTL8188EU_TX_DESCRIPTOR_MACID_MASK 0x1f
#define RTL8188EU_TX_DESCRIPTOR_ENABLE_DESCRIPTOR_ID (1u << 21)
#define RTL8188EU_TX_DESCRIPTOR_SECURITY_TKIP 0x00400000
#define RTL8188EU_TX_DESCRIPTOR_SECURITY_AES 0x00c00000

#define RTL8188EU_TX_DESCRIPTOR_OWN (1u << 7)
#define RTL8188EU_TX_DESCRIPTOR_FIRST_SEGMENT (1u << 3)
#define RTL8188EU_TX_DESCRIPTOR_LAST_SEGMENT (1u << 2)
#define RTL8188EU_TX_DESCRIPTOR_BROADCAST_MULTICAST (1u << 0)
#define RTL8188EU_TX_DESCRIPTOR_AGGREGATION_BREAK (1u << 16)
#define RTL8188EU_TX_DESCRIPTOR_ANTENNA_A (1u << 24)
#define RTL8188EU_TX_DESCRIPTOR_ANTENNA_B (1u << 25)
#define RTL8188EU_TX_DESCRIPTOR_ANTENNA_C (1u << 29)
#define RTL8188EU_TX_DESCRIPTOR_USE_DRIVER_RATE (1u << 8)
#define RTL8188EU_TX_DESCRIPTOR_DATA_FALLBACK_LIMITS 0x0001ff00
#define RTL8188EU_TX_DESCRIPTOR_RETRY_LIMIT_ENABLE (1u << 17)
#define RTL8188EU_TX_DESCRIPTOR_RETRY_LIMIT_SHIFT 18
#define RTL8188EU_TX_MANAGEMENT_RETRY_LIMIT 6

static uint8_t data_endpoint(const u80211_drv_rtl8188eu_t *rtl8188eu) {
	if (rtl8188eu->bulk_out_endpoint_count >= 3)
		return rtl8188eu->tx_endpoint_low;

	if (rtl8188eu->bulk_out_endpoint_count == 2)
		return rtl8188eu->tx_endpoint_normal;

	return rtl8188eu->tx_endpoint_high;
}

static void calculate_checksum(uint8_t *descriptor) {
	uint16_t checksum = 0;
	for (size_t i = 0; i < RTL8188EU_TX_DESCRIPTOR_SIZE; i += sizeof(uint16_t))
		checksum ^= u80211_drv_deserialize_le16(descriptor + i);

	u80211_drv_serialize_le16(descriptor + 28, checksum);
}

static int build_descriptor(const uint8_t *frame, size_t frame_size, const u80211_drv_transmit_options_t *options, uint8_t *descriptor, uint8_t *queue_out) {
	int key_index = options == NULL ? -1 : options->key;
	int cipher = options == NULL ? U80211_DRV_CIPHER_NONE : options->cipher;

	if (frame_size < U80211_DRV_80211_HEADER_MINIMUM_SIZE)
		return U80211_DRV_STATUS_MALFORMED_PACKET;

	if (key_index < -1 || key_index > U80211_DRV_RTL8188EU_CAM_CTL0_KEY_ID_MASK)
		return U80211_DRV_STATUS_INVALID_ARGUMENT;

	if ((key_index < 0) != (cipher == U80211_DRV_CIPHER_NONE))
		return U80211_DRV_STATUS_INVALID_ARGUMENT;

	uint16_t frame_control = u80211_drv_80211_frame_control(frame);
	uint8_t frame_type = u80211_drv_80211_frame_type(frame_control);
	uint8_t frame_subtype = u80211_drv_80211_frame_subtype(frame_control);
	uint8_t queue;

	if (frame_type == U80211_DRV_80211_FRAME_TYPE_MANAGEMENT)
		queue = RTL8188EU_TX_QUEUE_MANAGEMENT;
	else if (frame_type == U80211_DRV_80211_FRAME_TYPE_DATA && (frame_subtype & U80211_DRV_80211_FRAME_DATA_QOS_BIT) == 0)
		queue = RTL8188EU_TX_QUEUE_BEST_EFFORT;
	else
		return U80211_DRV_STATUS_NOT_SUPPORTED;

	u80211_drv_memset(descriptor, 0, RTL8188EU_TX_DESCRIPTOR_SIZE);
	u80211_drv_serialize_le16(descriptor, (uint16_t)frame_size); // packet size
	descriptor[2] = RTL8188EU_TX_DESCRIPTOR_SIZE; // packet start offset
	// once HT is implemented, that stuff would go here.
	descriptor[3] = RTL8188EU_TX_DESCRIPTOR_OWN | RTL8188EU_TX_DESCRIPTOR_FIRST_SEGMENT | RTL8188EU_TX_DESCRIPTOR_LAST_SEGMENT;

	const uint8_t *receiver = u80211_drv_80211_receiver_address(frame);
	bool group_addressed = (receiver[0] & 1u) != 0;
	if (group_addressed)
		descriptor[3] |= RTL8188EU_TX_DESCRIPTOR_BROADCAST_MULTICAST;

	// use the mixed 11b/g table for unicast data and the basic 11b table for management and group-addressed traffic.
	uint8_t raid = frame_type == U80211_DRV_80211_FRAME_TYPE_DATA && !group_addressed ? RTL8188EU_TX_RAID_11BG : RTL8188EU_TX_RAID_11B;
	uint32_t descriptor1 = ((uint32_t)queue << 8) | ((uint32_t)raid << RTL8188EU_TX_RAID_SHIFT);
	if (key_index >= 0) {
		// use hardware encryption. set the selected cipher bit
		if (frame_type != U80211_DRV_80211_FRAME_TYPE_DATA)
			return U80211_DRV_STATUS_INVALID_ARGUMENT;

		if (cipher == U80211_DRV_CIPHER_CCMP)
			descriptor1 |= RTL8188EU_TX_DESCRIPTOR_SECURITY_AES;
		else if (cipher == U80211_DRV_CIPHER_TKIP)
			descriptor1 |= RTL8188EU_TX_DESCRIPTOR_SECURITY_TKIP;
		else
			return U80211_DRV_STATUS_NOT_SUPPORTED;

		if (group_addressed) {
			descriptor1 |= RTL8188EU_TX_DESCRIPTOR_ENABLE_DESCRIPTOR_ID;
			descriptor1 |= (uint32_t)key_index & RTL8188EU_TX_DESCRIPTOR_MACID_MASK;
		}
	}
	u80211_drv_serialize_le32(descriptor + 4, descriptor1);

	// do not combine packets and use a known working setting for antennas (0b111, last bit is set in dword 7)
	u80211_drv_serialize_le32(descriptor + 8, RTL8188EU_TX_DESCRIPTOR_AGGREGATION_BREAK | RTL8188EU_TX_DESCRIPTOR_ANTENNA_A | RTL8188EU_TX_DESCRIPTOR_ANTENNA_B);

	uint16_t sequence_number = u80211_drv_80211_sequence_number(frame);
	u80211_drv_serialize_le32(descriptor + 12, (uint32_t)sequence_number << 16);

	// this controls a lot of things about how packets are transmitted. 
	// at the moment, we only care about telling it to use our selected rate.
	// this is a list of things of interest that would be handled here:
	// qos, hw sequence control, short preamble, different bandwidth stuff
	u80211_drv_serialize_le32(descriptor + 16, RTL8188EU_TX_DESCRIPTOR_USE_DRIVER_RATE);

	if (frame_type == U80211_DRV_80211_FRAME_TYPE_MANAGEMENT) {
		// six hardware retries @ 1mbps for management packets
		u80211_drv_serialize_le32(descriptor + 20, RTL8188EU_TX_DESCRIPTOR_RETRY_LIMIT_ENABLE | (RTL8188EU_TX_MANAGEMENT_RETRY_LIMIT << RTL8188EU_TX_DESCRIPTOR_RETRY_LIMIT_SHIFT));
	} else {
		// TODO: encode our selected rate here as well once selection is implemented
		// enable rate fallback for data packets (31 data retries + 15 request-to-send retries)
		u80211_drv_serialize_le32(descriptor + 20, RTL8188EU_TX_DESCRIPTOR_DATA_FALLBACK_LIMITS);
	}

	// dw6 would contain things related to usb aggregation. that is completely disabled in this driver, so it is skipped over.
	
	// dw7 has a checksum and the last bit of the antenna selector
	u80211_drv_serialize_le32(descriptor + 28, RTL8188EU_TX_DESCRIPTOR_ANTENNA_C);
	calculate_checksum(descriptor);
	*queue_out = queue;
	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_rtl8188eu_tx_buffer_allocate(size_t size, void **buffer) {
	if (size > U80211_DRV_80211_MAX_MPDU_SIZE)
		return U80211_DRV_STATUS_INVALID_ARGUMENT;

	size_t allocation_size = size + RTL8188EU_TX_DESCRIPTOR_SIZE;
	void *data = u80211_drv_kernel_allocate(allocation_size);
	if (data == NULL)
		return U80211_DRV_STATUS_OUT_OF_MEMORY;

	*buffer = (uint8_t *)data + RTL8188EU_TX_DESCRIPTOR_SIZE;
	return U80211_DRV_STATUS_SUCCESS;
}

void u80211_drv_rtl8188eu_tx_buffer_free(void *buffer) {
	u80211_drv_kernel_free((uint8_t *)buffer - RTL8188EU_TX_DESCRIPTOR_SIZE);
}

int u80211_drv_rtl8188eu_transmit(u80211_drv_rtl8188eu_t *rtl8188eu, void *buffer, size_t size, size_t current_offset, const u80211_drv_transmit_options_t *options) {
	uint8_t *frame = (uint8_t *)buffer + current_offset;
	size_t frame_size = size - current_offset;
	uint8_t *descriptor = frame - RTL8188EU_TX_DESCRIPTOR_SIZE;

	uint8_t queue;
	int status = build_descriptor(frame, frame_size, options, descriptor, &queue);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto cleanup;

	uint8_t endpoint = queue == RTL8188EU_TX_QUEUE_MANAGEMENT ? rtl8188eu->tx_endpoint_high : data_endpoint(rtl8188eu);
	size_t transfer_size = RTL8188EU_TX_DESCRIPTOR_SIZE + frame_size;
	size_t transferred_size = 0;
	status = u80211_drv_kernel_submit_bulk_xfer_and_wait(
		rtl8188eu->device,
		endpoint,
		descriptor,
		transfer_size,
		&transferred_size,
		RTL8188EU_TX_TIMEOUT_MS
	);

	if (status == U80211_DRV_STATUS_SUCCESS && transferred_size != transfer_size)
		status = U80211_DRV_STATUS_UNKNOWN_ERROR;

cleanup:
	u80211_drv_rtl8188eu_tx_buffer_free(buffer);
	return status;
}
