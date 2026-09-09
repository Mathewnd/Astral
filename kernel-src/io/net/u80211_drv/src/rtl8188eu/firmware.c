#include <stddef.h>
#include <stdint.h>

#include <u80211_drv/rtl8188eu.h>
#include <u80211_drv/status.h>

#define RTL8188EU_FIRMWARE_SIGNATURE  0x88e
#define RTL8188EU_FIRMWARE_PREPARE_DELAY_US  50
#define RTL8188EU_FIRMWARE_WRITE_SIZE  196
#define RTL8188EU_FIRMWARE_PAGE_COUNT  8
#define RTL8188EU_FIRMWARE_MAX_POLLS  1000
#define RTL8188EU_FIRMWARE_CHECKSUM_POLL_DELAY_US  5
#define RTL8188EU_FIRMWARE_READY_POLL_DELAY_US  10

static int firmware_reset(u80211_drv_device_handle_t device) {
	// hold mcu wrapper
	uint8_t value8;
	int status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_RSV_CTRL, &value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value8 &= ~U80211_DRV_RTL8188EU_REG_RSV_CTRL_WLOCK_00;
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_RSV_CTRL, value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// assert wrapper reset
	uint16_t value16;
	status = u80211_drv_rtl8188eu_reg_read16(device, U80211_DRV_RTL8188EU_REG_RSV_CTRL, &value16);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value16 &= ~U80211_DRV_RTL8188EU_REG_RSV_CTRL_MCU_RST;
	status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_RSV_CTRL, value16);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// assert cpu reset
	status = u80211_drv_rtl8188eu_reg_read16(device, U80211_DRV_RTL8188EU_REG_SYS_FUNC, &value16);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value16 &= ~U80211_DRV_RTL8188EU_REG_SYS_FUNC_CPUEN;
	status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_SYS_FUNC, value16);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// hold mcu wrapper
	status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_RSV_CTRL, &value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value8 &= ~U80211_DRV_RTL8188EU_REG_RSV_CTRL_WLOCK_00;
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_RSV_CTRL, value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// deassert mcu wrapper reset
	status = u80211_drv_rtl8188eu_reg_read16(device, U80211_DRV_RTL8188EU_REG_RSV_CTRL, &value16);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value16 |= U80211_DRV_RTL8188EU_REG_RSV_CTRL_MCU_RST;
	status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_RSV_CTRL, value16);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// deassert cpu reset
	status = u80211_drv_rtl8188eu_reg_read16(device, U80211_DRV_RTL8188EU_REG_SYS_FUNC, &value16);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value16 |= U80211_DRV_RTL8188EU_REG_SYS_FUNC_CPUEN;
	return u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_SYS_FUNC, value16);
}

int u80211_drv_rtl8188eu_firmware_prepare(u80211_drv_device_handle_t device, const void *firmware_data, size_t firmware_size, const uint8_t **firmware_payload, size_t *firmware_payload_size) {
	// verify if the firmware is ok
	const uint8_t *payload = firmware_data;
	size_t payload_size = firmware_size;
	uint16_t signature = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
	if ((signature >> 4) == RTL8188EU_FIRMWARE_SIGNATURE) {
		payload += U80211_DRV_RTL8188EU_FIRMWARE_HEADER_SIZE;
		payload_size -= U80211_DRV_RTL8188EU_FIRMWARE_HEADER_SIZE;
	}

	// check if there is stale ram in the mcu
	uint8_t value8;
	int status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, &value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	if ((value8 & U80211_DRV_RTL8188EU_REG_MCUFWDL_RAM_DOWNLOAD_SELECT) != 0) {
		// clear stale ram and reset firmware
		status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, 0);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;

		status = firmware_reset(device);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;
	}

	// enable firmware download
	status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, &value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value8 |= U80211_DRV_RTL8188EU_REG_MCUFWDL_ENABLE;
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint32_t value32;
	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, &value32);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value32 &= ~U80211_DRV_RTL8188EU_REG_MCUFWDL_ROM_DOWNLOAD_LENGTH;
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, value32);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// reset firmware checksum
	status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, &value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value8 |= U80211_DRV_RTL8188EU_REG_MCUFWDL_CHECKSUM_REPORT;
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	u80211_drv_kernel_stall_us(RTL8188EU_FIRMWARE_PREPARE_DELAY_US);
	*firmware_payload = payload;
	*firmware_payload_size = payload_size;
	return U80211_DRV_STATUS_SUCCESS;
}

static int firmware_write_page(u80211_drv_device_handle_t device, unsigned int page, const uint8_t *data, size_t size) {
	// set the page selector
	uint32_t value;
	int status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value &= ~U80211_DRV_RTL8188EU_REG_MCUFWDL_PAGE_MASK;
	value |= (uint32_t)page << 16;
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// write firmware in 196-byte chunks.
	// this is actually needed because some chips apparently have issues
	// with different-sized writes: https://github.com/a5a5aa555oo/rtl8xxxu/issues/2
	uint16_t address = U80211_DRV_RTL8188EU_FIRMWARE_START_ADDRESS;
	while (size >= RTL8188EU_FIRMWARE_WRITE_SIZE) {
		status = u80211_drv_rtl8188eu_reg_write_region(device, address, data, RTL8188EU_FIRMWARE_WRITE_SIZE);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;

		address += RTL8188EU_FIRMWARE_WRITE_SIZE;
		data += RTL8188EU_FIRMWARE_WRITE_SIZE;
		size -= RTL8188EU_FIRMWARE_WRITE_SIZE;
	}

	if (size != 0)
		return u80211_drv_rtl8188eu_reg_write_region(device, address, data, (uint16_t)size);

	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_rtl8188eu_firmware_upload(u80211_drv_device_handle_t device, const uint8_t *firmware_payload, size_t firmware_payload_size) {
	if (firmware_payload_size > U80211_DRV_RTL8188EU_FIRMWARE_PAGE_SIZE * RTL8188EU_FIRMWARE_PAGE_COUNT)
		return U80211_DRV_STATUS_INVALID_ARGUMENT;

	unsigned int page = 0;
	while (firmware_payload_size != 0) {
		size_t page_size = firmware_payload_size;
		if (page_size > U80211_DRV_RTL8188EU_FIRMWARE_PAGE_SIZE)
			page_size = U80211_DRV_RTL8188EU_FIRMWARE_PAGE_SIZE;

		int status = firmware_write_page(device, page, firmware_payload, page_size);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;

		firmware_payload += page_size;
		firmware_payload_size -= page_size;
		++page;
	}

	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_rtl8188eu_firmware_start(u80211_drv_device_handle_t device) {
	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: waiting for firmware checksum");

	uint32_t value32;
	unsigned int poll;
	int status;
	for (poll = 0; poll < RTL8188EU_FIRMWARE_MAX_POLLS; ++poll) {
		status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, &value32);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;

		if ((value32 & U80211_DRV_RTL8188EU_REG_MCUFWDL_CHECKSUM_REPORT) != 0)
			break;

		u80211_drv_kernel_stall_us(RTL8188EU_FIRMWARE_CHECKSUM_POLL_DELAY_US);
	}

	if (poll == RTL8188EU_FIRMWARE_MAX_POLLS) {
		u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_ERROR, "rtl8188eu: firmware checksum timed out");
		return U80211_DRV_STATUS_TIMEOUT;
	}

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: firmware checksum ready");

	// leave firmware download mode
	uint8_t value8;
	status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, &value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value8 &= ~U80211_DRV_RTL8188EU_REG_MCUFWDL_ENABLE;
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_MCUFWDL + 1, 0);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// mark the downloaded image ready and discard stale firmware state
	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, &value32);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value32 &= ~U80211_DRV_RTL8188EU_REG_MCUFWDL_WINTINI_READY;
	value32 |= U80211_DRV_RTL8188EU_REG_MCUFWDL_READY;
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, value32);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = firmware_reset(device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: waiting for firmware ready");
	for (poll = 0; poll < RTL8188EU_FIRMWARE_MAX_POLLS; ++poll) {
		status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_MCUFWDL, &value32);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;

		if ((value32 & U80211_DRV_RTL8188EU_REG_MCUFWDL_WINTINI_READY) != 0) {
			u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: firmware ready");
			return U80211_DRV_STATUS_SUCCESS;
		}

		u80211_drv_kernel_stall_us(RTL8188EU_FIRMWARE_READY_POLL_DELAY_US);
	}

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_ERROR, "rtl8188eu: firmware ready timed out");
	return U80211_DRV_STATUS_TIMEOUT;
}
