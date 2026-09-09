#include <u80211_drv/drv_init.h>
#include <u80211_drv/rtl8188eu.h>
#include <u80211_drv/status.h>

static int u80211_drv_rtl8188eu_led_enable_activity(u80211_drv_device_handle_t device) {
	uint8_t ledcfg;
	int status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_LEDCFG2, &ledcfg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	ledcfg &= ~U80211_DRV_RTL8188EU_REG_LEDCFG2_SW_LED_DISABLE;
	ledcfg |= U80211_DRV_RTL8188EU_REG_LEDCFG2_HW_LED_CONTROL | U80211_DRV_RTL8188EU_REG_LEDCFG2_HW_LED_ENABLE;
	return u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_LEDCFG2, ledcfg);
}

static int transmit(void *device, void *buffer, size_t size, size_t current_offset, const u80211_drv_transmit_options_t *options) {
	return u80211_drv_rtl8188eu_transmit(device, buffer, size, current_offset, options);
}

static int set_channel(void *device, uint8_t channel) {
	return u80211_drv_rtl8188eu_set_channel(device, channel);
}

static int set_key(void *device, const u80211_drv_key_t *key) {
	return u80211_drv_rtl8188eu_set_key(device, key);
}

static int del_key(void *device, uint8_t index) {
	return u80211_drv_rtl8188eu_del_key(device, index);
}

static const u80211_drv_device_ops_t device_ops = {
	.allocate_tx_buffer = u80211_drv_rtl8188eu_tx_buffer_allocate,
	.free_tx_buffer = u80211_drv_rtl8188eu_tx_buffer_free,
	.transmit = transmit,
	.set_channel = set_channel,
	.set_key = set_key,
	.del_key = del_key,
};

static int discover_bulk_endpoints(u80211_drv_rtl8188eu_t *rtl8188eu) {
	u80211_drv_interface_descriptor_t interface_descriptor;
	int status = u80211_drv_kernel_get_interface_descriptor(rtl8188eu->interface, &interface_descriptor);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	if (interface_descriptor.endpoint_count == 0)
		return U80211_DRV_STATUS_NOT_SUPPORTED;

	u80211_drv_endpoint_descriptor_t *endpoints = u80211_drv_kernel_allocate(interface_descriptor.endpoint_count * sizeof(*endpoints));
	if (endpoints == NULL)
		return U80211_DRV_STATUS_OUT_OF_MEMORY;

	status = u80211_drv_kernel_get_endpoints(rtl8188eu->interface, endpoints, interface_descriptor.endpoint_count);
	if (status != U80211_DRV_STATUS_SUCCESS) {
		u80211_drv_kernel_free(endpoints);
		return status;
	}

	rtl8188eu->bulk_out_endpoint_count = 0;
	rtl8188eu->rx_endpoint = 0;
	rtl8188eu->tx_endpoint_high = 0;
	rtl8188eu->tx_endpoint_normal = 0;
	rtl8188eu->tx_endpoint_low = 0;

	for (uint8_t i = 0; i < interface_descriptor.endpoint_count; ++i) {
		if ((endpoints[i].attributes & U80211_DRV_KERNEL_ENDPOINT_TRANSFER_TYPE_MASK) != U80211_DRV_KERNEL_ENDPOINT_TRANSFER_TYPE_BULK)
			continue;
		if ((endpoints[i].address & U80211_DRV_KERNEL_XFER_DIRECTION_MASK) == U80211_DRV_KERNEL_XFER_IN) {
			if (rtl8188eu->rx_endpoint == 0)
				rtl8188eu->rx_endpoint = endpoints[i].address;
			continue;
		}

		switch (rtl8188eu->bulk_out_endpoint_count) {
			case 0:
				rtl8188eu->tx_endpoint_high = endpoints[i].address;
				break;
			case 1:
				rtl8188eu->tx_endpoint_normal = endpoints[i].address;
				break;
			case 2:
				rtl8188eu->tx_endpoint_low = endpoints[i].address;
				break;
		}

		++rtl8188eu->bulk_out_endpoint_count;
	}

	u80211_drv_kernel_free(endpoints);

	if (rtl8188eu->rx_endpoint == 0 || rtl8188eu->bulk_out_endpoint_count == 0)
		return U80211_DRV_STATUS_NOT_SUPPORTED;

	return U80211_DRV_STATUS_SUCCESS;
}

static void firmware_loaded(void *context, const void *firmware_data, size_t firmware_size) {
	u80211_drv_rtl8188eu_t *rtl8188eu = context;
	// the firmware is now in memory, we can continue initializing the chip.

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: found rtl8188eufw.bin");
	int status = u80211_drv_rtl8188eu_power_active(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	status = u80211_drv_rtl8188eu_mac_enable_infrastructure(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	status = u80211_drv_rtl8188eu_mac_configure_tx_queues(rtl8188eu->device, rtl8188eu->bulk_out_endpoint_count);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	status = u80211_drv_rtl8188eu_mac_configure_rx_fifo_boundary(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: uploading firmware");

	// upload the firmware into the MCU
	const uint8_t *firmware_payload;
	size_t firmware_payload_size;
	status = u80211_drv_rtl8188eu_firmware_prepare(
		rtl8188eu->device,
		firmware_data,
		firmware_size,
		&firmware_payload,
		&firmware_payload_size
	);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	status = u80211_drv_rtl8188eu_firmware_upload(rtl8188eu->device, firmware_payload, firmware_payload_size);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	status = u80211_drv_rtl8188eu_firmware_start(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: firmware initialized");

	status = u80211_drv_rtl8188eu_mac_load_table(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: MAC table initialized");

	status = u80211_drv_rtl8188eu_bb_enable(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	status = u80211_drv_rtl8188eu_bb_load_table(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: BB table initialized");

	status = u80211_drv_rtl8188eu_bb_load_agc_table(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: AGC table initialized");

	status = u80211_drv_rtl8188eu_bb_apply_efuse_calibration(rtl8188eu);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: EFUSE crystal calibration applied");

	status = u80211_drv_rtl8188eu_rf_load_table(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: RF table initialized");

	status = u80211_drv_rtl8188eu_rf_read(rtl8188eu->device, U80211_DRV_RTL8188EU_RF_CHNLBW, &rtl8188eu->rf_chnlbw);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: RF channel state cached");

	status = u80211_drv_rtl8188eu_mac_configure_packet_buffer(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: packet buffer configured");

	status = u80211_drv_rtl8188eu_mac_initialize_llt(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: LLT initialized");

	status = u80211_drv_rtl8188eu_mac_enable_tx_rx(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: MAC TX and RX enabled");

	status = u80211_drv_rtl8188eu_mac_disable_rx_aggregation(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: RX aggregation disabled");

	status = u80211_drv_rtl8188eu_mac_configure_wmac(rtl8188eu);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: WMAC configured");

	status = u80211_drv_rtl8188eu_mac_configure_timing(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: timing configured");

	status = u80211_drv_rtl8188eu_bb_enable_datapaths(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: BB datapaths enabled");

	status = u80211_drv_rtl8188eu_mac_configure_hardware_offloads(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: hardware offloads configured");

	status = u80211_drv_rtl8188eu_calibrate(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: RF calibration complete");

	status = u80211_drv_rtl8188eu_set_channel(rtl8188eu, 1);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: channel 1 configured");

	status = u80211_drv_rtl8188eu_led_enable_activity(rtl8188eu->device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: activity LED enabled");

	status = u80211_drv_rtl8188eu_rx_start(rtl8188eu);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: RX started");

	u80211_drv_device_metadata_t metadata = {
		.rate_bitmap = {
			0x14, // 1 and 2 Mbps
			0x18, // 5.5 and 6 Mbps
			0x44, // 9 and 11 Mbps
			0x01, // 12 Mbps
			0x10, // 18 Mbps
			0x00,
			0x01, // 24 Mbps
			0x00,
			0x00,
			0x01, // 36 Mbps
			0x00,
			0x00,
			0x01, // 48 Mbps
			0x10, // 54 Mbps
			0x00,
			0x00,
		},
	};
	for (size_t i = 0; i < U80211_DRV_DEVICE_MAC_ADDRESS_LEN; ++i)
		metadata.mac_address[i] = rtl8188eu->efuse.mac_address[i];

	status = u80211_drv_device_ready(rtl8188eu, &metadata, &device_ops, &rtl8188eu->network_device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		goto error;

	return;
error:
	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_ERROR, "rtl8188eu: post firmware initialization failed");
}

int u80211_drv_rtl8188eu_init(u80211_drv_device_handle_t device, u80211_drv_interface_handle_t interface) {
	uint32_t sys_cfg;
	int status;

	// some cuts are not supported by this driver, check if we are attaching one of them
	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_SYS_CFG, &sys_cfg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	if ((sys_cfg & U80211_DRV_RTL8188EU_REG_SYS_CFG_TRP_VAUX_EN) != 0 || U80211_DRV_RTL8188EU_REG_SYS_CFG_VER(sys_cfg) == 8)
		return U80211_DRV_STATUS_NOT_SUPPORTED;

	u80211_drv_rtl8188eu_t *rtl8188eu = u80211_drv_kernel_allocate(sizeof(*rtl8188eu));
	if (rtl8188eu == NULL)
		return U80211_DRV_STATUS_OUT_OF_MEMORY;

	rtl8188eu->device = device;
	rtl8188eu->interface = interface;
	__atomic_store_n(&rtl8188eu->network_device, NULL, __ATOMIC_RELAXED);

	// this is necessary to set up the TX queues and submit RX transfers later
	status = discover_bulk_endpoints(rtl8188eu);
	if (status != U80211_DRV_STATUS_SUCCESS) {
		u80211_drv_kernel_free(rtl8188eu);
		return status;
	}

	// read efuses to get information like the MAC address
	uint8_t *efuse_map = u80211_drv_kernel_allocate(U80211_DRV_RTL8188EU_EFUSE_MAP_LEN);
	if (efuse_map == NULL) {
		u80211_drv_kernel_free(rtl8188eu);
		return U80211_DRV_STATUS_OUT_OF_MEMORY;
	}

	status = u80211_drv_rtl8188eu_efuse_prepare(device);
	if (status != U80211_DRV_STATUS_SUCCESS) {
		u80211_drv_kernel_free(efuse_map);
		u80211_drv_kernel_free(rtl8188eu);
		return status;
	}

	status = u80211_drv_rtl8188eu_read_efuse(device, efuse_map);
	if (status != U80211_DRV_STATUS_SUCCESS) {
		u80211_drv_kernel_free(efuse_map);
		u80211_drv_kernel_free(rtl8188eu);
		return status;
	}

	status = u80211_drv_rtl8188eu_efuse_finish(device);
	if (status != U80211_DRV_STATUS_SUCCESS) {
		u80211_drv_kernel_free(efuse_map);
		u80211_drv_kernel_free(rtl8188eu);
		return status;
	}

	status = u80211_drv_rtl8188eu_parse_efuse(efuse_map, &rtl8188eu->efuse);
	if (status != U80211_DRV_STATUS_SUCCESS) {
		u80211_drv_kernel_free(efuse_map);
		u80211_drv_kernel_free(rtl8188eu);
		return status;
	}

	u80211_drv_kernel_free(efuse_map);

	// wait until firmware gets loaded from disk by the kernel
	// TODO: have a way of cancelling this wait for detach
	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: waiting for rtl8188eufw.bin");
	status = u80211_drv_kernel_get_firmware("rtl8188eufw.bin", firmware_loaded, rtl8188eu);
	if (status != U80211_DRV_STATUS_SUCCESS)
		u80211_drv_kernel_free(rtl8188eu);

	return status;
}
