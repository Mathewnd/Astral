#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <u80211_drv/80211.h>
#include <u80211_drv/kernel_interface.h>
#include <u80211_drv/rtl8188eu.h>
#include <u80211_drv/status.h>
#include <u80211_drv/util.h>

#define RTL8188EU_RX_DESCRIPTOR_SIZE 24
#define RTL8188EU_RX_BUFFER_SIZE (16 * 1024)

#define RTL8188EU_RX_DESCRIPTOR_PACKET_LENGTH_MASK 0x00003fff
#define RTL8188EU_RX_DESCRIPTOR_CRC_ERROR (1u << 14)
#define RTL8188EU_RX_DESCRIPTOR_ICV_ERROR (1u << 15)
#define RTL8188EU_RX_DESCRIPTOR_DRVINFO_SIZE_MASK 0x000f0000
#define RTL8188EU_RX_DESCRIPTOR_DRVINFO_SIZE_SHIFT 16
#define RTL8188EU_RX_DESCRIPTOR_SECURITY_MASK 0x00700000
#define RTL8188EU_RX_DESCRIPTOR_SECURITY_SHIFT 20
#define RTL8188EU_RX_DESCRIPTOR_SECURITY_NONE 0
#define RTL8188EU_RX_DESCRIPTOR_SECURITY_TKIP 2
#define RTL8188EU_RX_DESCRIPTOR_SECURITY_AES 4
#define RTL8188EU_RX_DESCRIPTOR_SHIFT_MASK 0x03000000
#define RTL8188EU_RX_DESCRIPTOR_SHIFT_SHIFT 24
#define RTL8188EU_RX_DESCRIPTOR_SOFTWARE_DECRYPTED (1u << 27)
#define RTL8188EU_RX_DESCRIPTOR_REPORT_SELECT_MASK 0x0000c000

#define RTL8188EU_RX_DESCRIPTOR_PACKET_COUNT_MASK 0x00ff0000
#define RTL8188EU_RX_DESCRIPTOR_PACKET_COUNT_SHIFT 16
#define RTL8188EU_RX_PACKET_ALIGNMENT 128

static size_t align_rx_packet(size_t size) {
	return (size + RTL8188EU_RX_PACKET_ALIGNMENT - 1) & ~(RTL8188EU_RX_PACKET_ALIGNMENT - 1);
}

static void process_rx_buffer(u80211_drv_rtl8188eu_t *rtl8188eu, void *buffer, size_t size) {
	uint8_t *bytes = buffer;
	size_t offset = 0;
	size_t packet_count = 0;

	while (size - offset >= RTL8188EU_RX_DESCRIPTOR_SIZE) {
		uint8_t *descriptor = bytes + offset;
		uint32_t descriptor0 = u80211_drv_deserialize_le32(descriptor);
		uint32_t descriptor2 = u80211_drv_deserialize_le32(descriptor + 8);
		uint32_t descriptor3 = u80211_drv_deserialize_le32(descriptor + 12);
		// there might be many packets in one transfer.
		if (packet_count == 0) {
			packet_count = (descriptor2 & RTL8188EU_RX_DESCRIPTOR_PACKET_COUNT_MASK) >> RTL8188EU_RX_DESCRIPTOR_PACKET_COUNT_SHIFT;
			if (packet_count == 0)
				packet_count = 1;
		}

		// TODO: extract channel?
		// TODO: extract rate?
		// TODO: get dbm info from phy data?
		bool is_rx_record = (descriptor3 & RTL8188EU_RX_DESCRIPTOR_REPORT_SELECT_MASK) == 0;
		bool crc_icv_error = descriptor0 & (RTL8188EU_RX_DESCRIPTOR_CRC_ERROR | RTL8188EU_RX_DESCRIPTOR_ICV_ERROR);
		uint32_t security = (descriptor0 & RTL8188EU_RX_DESCRIPTOR_SECURITY_MASK) >> RTL8188EU_RX_DESCRIPTOR_SECURITY_SHIFT;
		bool software_decryption_required = (descriptor0 & RTL8188EU_RX_DESCRIPTOR_SOFTWARE_DECRYPTED) != 0;
		size_t packet_size = descriptor0 & RTL8188EU_RX_DESCRIPTOR_PACKET_LENGTH_MASK;
		size_t drvinfo_size = ((descriptor0 & RTL8188EU_RX_DESCRIPTOR_DRVINFO_SIZE_MASK) >> RTL8188EU_RX_DESCRIPTOR_DRVINFO_SIZE_SHIFT) * 8;
		size_t descriptor_shift = (descriptor0 & RTL8188EU_RX_DESCRIPTOR_SHIFT_MASK) >> RTL8188EU_RX_DESCRIPTOR_SHIFT_SHIFT;
		size_t packet_offset = RTL8188EU_RX_DESCRIPTOR_SIZE + drvinfo_size + descriptor_shift;
		size_t remaining = size - offset;

		if (packet_offset > remaining || packet_size > remaining - packet_offset)
			break;

		if (packet_size != 0 && packet_size <= U80211_DRV_80211_MAX_MPDU_SIZE && !crc_icv_error && is_rx_record) {
			uint8_t *packet = descriptor + packet_offset;
			// u80211 consumes the cipher header and trailer after hardware has decrypted the payload.
			if (security != RTL8188EU_RX_DESCRIPTOR_SECURITY_NONE &&
				((security != RTL8188EU_RX_DESCRIPTOR_SECURITY_TKIP && security != RTL8188EU_RX_DESCRIPTOR_SECURITY_AES) ||
				software_decryption_required))
				goto next_packet;

			u80211_drv_network_device_handle_t network_device = __atomic_load_n(&rtl8188eu->network_device, __ATOMIC_ACQUIRE);
			if (network_device != NULL)
				u80211_drv_packet_received(network_device, packet, packet_size);
		}

	next_packet:
		--packet_count;
		if (packet_count == 0)
			break;

		size_t unit_size = align_rx_packet(packet_offset + packet_size);
		if (unit_size > remaining)
			break;
		offset += unit_size;
	}
}

static void rx_complete(void *context, int status, size_t transferred_size) {
	u80211_drv_rtl8188eu_rx_slot_t *slot = context;
	u80211_drv_rtl8188eu_t *rtl8188eu = slot->rtl8188eu;

	if (status == U80211_DRV_STATUS_SUCCESS)
		process_rx_buffer(rtl8188eu, slot->buffer, transferred_size);

	status = u80211_drv_kernel_submit_xfer(slot->transfer);
	if (status != U80211_DRV_STATUS_SUCCESS)
		u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_WARN, "rtl8188eu: failed to resubmit RX transfer");
}

int u80211_drv_rtl8188eu_rx_start(u80211_drv_rtl8188eu_t *rtl8188eu) {
	for (size_t i = 0; i < U80211_DRV_RTL8188EU_RX_SLOT_COUNT; ++i) {
		u80211_drv_rtl8188eu_rx_slot_t *slot = &rtl8188eu->rx_slots[i];
		slot->rtl8188eu = rtl8188eu;
		slot->buffer = NULL;
		slot->transfer = NULL;
	}

	int status = U80211_DRV_STATUS_SUCCESS;
	for (size_t i = 0; i < U80211_DRV_RTL8188EU_RX_SLOT_COUNT; ++i) {
		u80211_drv_rtl8188eu_rx_slot_t *slot = &rtl8188eu->rx_slots[i];
		slot->buffer = u80211_drv_kernel_allocate(RTL8188EU_RX_BUFFER_SIZE);
		if (slot->buffer == NULL) {
			status = U80211_DRV_STATUS_OUT_OF_MEMORY;
			break;
		}

		status = u80211_drv_kernel_allocate_bulk_xfer(
			rtl8188eu->device,
			rtl8188eu->rx_endpoint,
			slot->buffer,
			RTL8188EU_RX_BUFFER_SIZE,
			rx_complete,
			slot,
			&slot->transfer
		);
		if (status != U80211_DRV_STATUS_SUCCESS)
			break;

		status = u80211_drv_kernel_submit_xfer(slot->transfer);
		if (status != U80211_DRV_STATUS_SUCCESS)
			break;
	}

	return status;
}
