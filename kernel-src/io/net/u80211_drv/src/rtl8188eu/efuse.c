#include <stdint.h>

#include <u80211_drv/kernel_interface.h>
#include <u80211_drv/rtl8188eu.h>
#include <u80211_drv/status.h>
#include <u80211_drv/string.h>

#define EFUSE_ADDRESS_HIGH_MASK 0x03
#define EFUSE_CONTROL_READ_READY (1u << 7)
#define EFUSE_MAX_POLLS 500
#define EFUSE_READ_SETTLE_US 50
#define EFUSE_SECTION_LEN (U80211_DRV_RTL8188EU_EFUSE_WORDS_PER_SECTION * 2)
#define EFUSE_SECTION_COUNT (U80211_DRV_RTL8188EU_EFUSE_MAP_LEN / EFUSE_SECTION_LEN)
#define EFUSE_RTL_ID_OFFSET 0x000
#define EFUSE_CCK_TX_POWER_BASE_INDEX_OFFSET 0x010
#define EFUSE_HT40_1S_TX_POWER_BASE_INDEX_OFFSET 0x016
#define EFUSE_HT20_OFDM_TX_POWER_DIFF_OFFSET 0x01b
#define EFUSE_XTAL_K_OFFSET 0x0b9
#define EFUSE_MAC_ADDRESS_OFFSET 0x0d7

int u80211_drv_rtl8188eu_efuse_prepare(u80211_drv_device_handle_t device) {
	uint16_t value;
	int status;

	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_EFUSE_ACCESS, U80211_DRV_RTL8188EU_EFUSE_ACCESS_ENABLE);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read16(device, U80211_DRV_RTL8188EU_REG_SYS_ISO_CTRL, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	if ((value & U80211_DRV_RTL8188EU_REG_SYS_ISO_CTRL_PWC_EV12V) == 0) {
		value |= U80211_DRV_RTL8188EU_REG_SYS_ISO_CTRL_PWC_EV12V;
		status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_SYS_ISO_CTRL, value);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;
	}

	status = u80211_drv_rtl8188eu_reg_read16(device, U80211_DRV_RTL8188EU_REG_SYS_FUNC, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	if ((value & U80211_DRV_RTL8188EU_REG_SYS_FUNC_ELDR) == 0) {
		value |= U80211_DRV_RTL8188EU_REG_SYS_FUNC_ELDR;
		status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_SYS_FUNC, value);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;
	}

	status = u80211_drv_rtl8188eu_reg_read16(device, U80211_DRV_RTL8188EU_REG_SYS_CLKR, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint16_t required_clocks = U80211_DRV_RTL8188EU_REG_SYS_CLKR_LOADER_ENABLE | U80211_DRV_RTL8188EU_REG_SYS_CLKR_ANA8M;
	if ((value & required_clocks) != required_clocks) {
		value |= required_clocks;
		status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_SYS_CLKR, value);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;
	}

	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_rtl8188eu_efuse_finish(u80211_drv_device_handle_t device) {
	return u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_EFUSE_ACCESS, U80211_DRV_RTL8188EU_EFUSE_ACCESS_DISABLE);
}

static int efuse_read_physical8(u80211_drv_device_handle_t device, uint16_t address, uint8_t *result) {
	if (address >= U80211_DRV_RTL8188EU_EFUSE_PHYSICAL_LEN)
		return U80211_DRV_STATUS_FAULTY_HARDWARE;

	int status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_EFUSE_CTRL + 1, address & 0xff);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint8_t value8;
	status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_EFUSE_CTRL + 2, &value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value8 &= ~EFUSE_ADDRESS_HIGH_MASK;
	value8 |= (address >> 8) & EFUSE_ADDRESS_HIGH_MASK;
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_EFUSE_CTRL + 2, value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_EFUSE_CTRL + 3, &value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value8 &= ~EFUSE_CONTROL_READ_READY;
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_EFUSE_CTRL + 3, value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint32_t value32;
	unsigned int poll;
	for (poll = 0; poll < EFUSE_MAX_POLLS; ++poll) {
		status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_EFUSE_CTRL, &value32);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;

		if ((value32 & U80211_DRV_RTL8188EU_REG_EFUSE_CTRL_READ_READY) != 0)
			break;
	}

	if (poll == EFUSE_MAX_POLLS)
		return U80211_DRV_STATUS_TIMEOUT;

	u80211_drv_kernel_stall_us(EFUSE_READ_SETTLE_US);

	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_EFUSE_CTRL, &value32);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	*result = value32 & 0xff;
	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_rtl8188eu_read_efuse(u80211_drv_device_handle_t device, uint8_t efuse_map[U80211_DRV_RTL8188EU_EFUSE_MAP_LEN]) {
	u80211_drv_memset(efuse_map, 0xff, U80211_DRV_RTL8188EU_EFUSE_MAP_LEN);

	uint16_t physical_address = 0;
	while (physical_address < U80211_DRV_RTL8188EU_EFUSE_PHYSICAL_LEN) {
		uint8_t header;
		int status = efuse_read_physical8(device, physical_address++, &header);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;

		if (header == 0xff)
			return U80211_DRV_STATUS_SUCCESS;

		unsigned int section;
		uint8_t word_mask;
		if ((header & 0x1f) == 0x0f) {
			section = (header & 0xe0) >> 5;

			if (physical_address >= U80211_DRV_RTL8188EU_EFUSE_PHYSICAL_LEN)
				return U80211_DRV_STATUS_FAULTY_HARDWARE;

			uint8_t extended_header;
			status = efuse_read_physical8(device, physical_address++, &extended_header);
			if (status != U80211_DRV_STATUS_SUCCESS)
				return status;

			if ((extended_header & 0x0f) == 0x0f)
				continue;

			section |= (extended_header & 0xf0) >> 1;
			word_mask = extended_header & 0x0f;
		} else {
			section = (header >> 4) & 0x0f;
			word_mask = header & 0x0f;
		}

		if (section >= EFUSE_SECTION_COUNT)
			return U80211_DRV_STATUS_FAULTY_HARDWARE;

		uint16_t logical_address = section * EFUSE_SECTION_LEN;
		for (unsigned int word = 0; word < U80211_DRV_RTL8188EU_EFUSE_WORDS_PER_SECTION; ++word) {
			if ((word_mask & (1u << word)) != 0) {
				logical_address += 2;
				continue;
			}

			if (logical_address + 1 >= U80211_DRV_RTL8188EU_EFUSE_MAP_LEN || physical_address + 1 >= U80211_DRV_RTL8188EU_EFUSE_PHYSICAL_LEN)
				return U80211_DRV_STATUS_FAULTY_HARDWARE;

			status = efuse_read_physical8(device, physical_address++, &efuse_map[logical_address++]);
			if (status != U80211_DRV_STATUS_SUCCESS)
				return status;

			status = efuse_read_physical8(device, physical_address++, &efuse_map[logical_address++]);
			if (status != U80211_DRV_STATUS_SUCCESS)
				return status;
		}
	}

	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_rtl8188eu_parse_efuse(const uint8_t efuse_map[U80211_DRV_RTL8188EU_EFUSE_MAP_LEN], u80211_drv_rtl8188eu_efuse_t *result) {
	uint16_t rtl_id = (uint16_t)efuse_map[EFUSE_RTL_ID_OFFSET] | ((uint16_t)efuse_map[EFUSE_RTL_ID_OFFSET + 1] << 8);
	if (rtl_id != U80211_DRV_RTL8188EU_EFUSE_RTL_ID)
		return U80211_DRV_STATUS_FAULTY_HARDWARE;

	result->rtl_id = rtl_id;
	for (unsigned int i = 0; i < U80211_DRV_RTL8188EU_MAC_ADDRESS_LEN; ++i)
		result->mac_address[i] = efuse_map[EFUSE_MAC_ADDRESS_OFFSET + i];

	for (unsigned int i = 0; i < U80211_DRV_RTL8188EU_CCK_TX_POWER_BASE_INDEX_COUNT; ++i)
		result->cck_tx_power_base_indexes[i] = efuse_map[EFUSE_CCK_TX_POWER_BASE_INDEX_OFFSET + i];

	for (unsigned int i = 0; i < U80211_DRV_RTL8188EU_HT40_1S_TX_POWER_BASE_INDEX_COUNT; ++i)
		result->ht40_1s_tx_power_base_indexes[i] = efuse_map[EFUSE_HT40_1S_TX_POWER_BASE_INDEX_OFFSET + i];
	result->ht20_ofdm_tx_power_diff = efuse_map[EFUSE_HT20_OFDM_TX_POWER_DIFF_OFFSET];

	result->xtal_k = efuse_map[EFUSE_XTAL_K_OFFSET] & 0x3f;
	return U80211_DRV_STATUS_SUCCESS;
}
