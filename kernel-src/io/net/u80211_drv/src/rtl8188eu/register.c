#include <stddef.h>
#include <stdint.h>

#include <u80211_drv/kernel_interface.h>
#include <u80211_drv/rtl8188eu.h>
#include <u80211_drv/status.h>
#include <u80211_drv/util.h>

enum {
	REALTEK_USB_READ = 0xc0,
	REALTEK_USB_WRITE = 0x40,
	REALTEK_USB_CMD_REQ = 0x05,
	REALTEK_USB_CMD_IDX = 0x00,
	REALTEK_USB_TIMEOUT_MS = 500,
};

static int reg_xfer(u80211_drv_device_handle_t device, uint8_t flags, uint16_t reg, void *data, uint16_t size) {
	size_t transferred = 0;
	int status = u80211_drv_kernel_submit_control_xfer_and_wait(
			device,
			flags,
			REALTEK_USB_CMD_REQ,
			reg,
			REALTEK_USB_CMD_IDX,
			data,
			size,
			&transferred,
			REALTEK_USB_TIMEOUT_MS
	);

	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	if (transferred != size)
		return U80211_DRV_STATUS_UNKNOWN_ERROR;

	return U80211_DRV_STATUS_SUCCESS;
}

static int reg_read(u80211_drv_device_handle_t device, uint16_t reg, void *value, uint16_t size) {
	return reg_xfer(device, REALTEK_USB_READ, reg, value, size);
}

int u80211_drv_rtl8188eu_reg_read8(u80211_drv_device_handle_t device, uint16_t reg, uint8_t *value) {
	return reg_read(device, reg, value, sizeof(*value));
}

int u80211_drv_rtl8188eu_reg_read16(u80211_drv_device_handle_t device, uint16_t reg, uint16_t *value) {
	uint16_t little_endian_value;
	int status = reg_read(device, reg, &little_endian_value, sizeof(little_endian_value));
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	*value = u80211_drv_le_to_host16(little_endian_value);
	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_rtl8188eu_reg_read32(u80211_drv_device_handle_t device, uint16_t reg, uint32_t *value) {
	uint32_t little_endian_value;
	int status = reg_read(device, reg, &little_endian_value, sizeof(little_endian_value));
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	*value = u80211_drv_le_to_host32(little_endian_value);
	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_rtl8188eu_reg_write8(u80211_drv_device_handle_t device, uint16_t reg, uint8_t value) {
	return reg_xfer(device, REALTEK_USB_WRITE, reg, &value, sizeof(value));
}

int u80211_drv_rtl8188eu_reg_write16(u80211_drv_device_handle_t device, uint16_t reg, uint16_t value) {
	uint16_t little_endian_value = u80211_drv_host_to_le16(value);
	return reg_xfer(device, REALTEK_USB_WRITE, reg, &little_endian_value, sizeof(little_endian_value));
}

int u80211_drv_rtl8188eu_reg_write32(u80211_drv_device_handle_t device, uint16_t reg, uint32_t value) {
	uint32_t little_endian_value = u80211_drv_host_to_le32(value);
	return reg_xfer(device, REALTEK_USB_WRITE, reg, &little_endian_value, sizeof(little_endian_value));
}

int u80211_drv_rtl8188eu_reg_write_region(u80211_drv_device_handle_t device, uint16_t reg, const void *data, uint16_t size) {
	return reg_xfer(device, REALTEK_USB_WRITE, reg, (void *)data, size);
}
