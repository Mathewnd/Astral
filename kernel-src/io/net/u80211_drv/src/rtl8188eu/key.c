#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <u80211_drv/kernel_interface.h>
#include <u80211_drv/rtl8188eu.h>
#include <u80211_drv/status.h>

#define RTL8188EU_CAM_WRITE_DELAY_US 100
#define RTL8188EU_CAM_KEY_WORD_FIRST 2
#define RTL8188EU_CAM_KEY_WORD_LAST 5

static int rtl8188eu_cam_write_word(u80211_drv_device_handle_t device, uint8_t entry, uint8_t word, uint32_t value) {
	uint32_t address = (uint32_t)entry * U80211_DRV_RTL8188EU_CAM_ENTRY_WORD_COUNT + word;
	int status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_CAMWRITE, value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(device,
		U80211_DRV_RTL8188EU_REG_CAMCMD,
		U80211_DRV_RTL8188EU_REG_CAMCMD_POLLING | U80211_DRV_RTL8188EU_REG_CAMCMD_WRITE | address
	);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	u80211_drv_kernel_stall_us(RTL8188EU_CAM_WRITE_DELAY_US);
	return U80211_DRV_STATUS_SUCCESS;
}

static int rtl8188eu_key_parameters(const u80211_drv_key_t *key, uint32_t *algorithm) {
	switch (key->cipher) {
		case U80211_DRV_CIPHER_CCMP:
			if (key->key_len != 16)
				return U80211_DRV_STATUS_INVALID_ARGUMENT;

			*algorithm = U80211_DRV_RTL8188EU_CAM_ALGORITHM_CCMP;
			return U80211_DRV_STATUS_SUCCESS;
		case U80211_DRV_CIPHER_TKIP:
			if (key->key_len != 32)
				return U80211_DRV_STATUS_INVALID_ARGUMENT;

			*algorithm = U80211_DRV_RTL8188EU_CAM_ALGORITHM_TKIP;
			return U80211_DRV_STATUS_SUCCESS;
		case U80211_DRV_CIPHER_WEP40:
			if (key->key_len != 5)
				return U80211_DRV_STATUS_INVALID_ARGUMENT;

			*algorithm = U80211_DRV_RTL8188EU_CAM_ALGORITHM_WEP40;
			return U80211_DRV_STATUS_SUCCESS;
		case U80211_DRV_CIPHER_WEP104:
			if (key->key_len != 13)
				return U80211_DRV_STATUS_INVALID_ARGUMENT;

			*algorithm = U80211_DRV_RTL8188EU_CAM_ALGORITHM_WEP104;
			return U80211_DRV_STATUS_SUCCESS;
		default:
			return U80211_DRV_STATUS_NOT_SUPPORTED;
	}
}

static uint32_t rtl8188eu_key_word(const u80211_drv_key_t *key, size_t offset) {
	uint32_t value = 0;
	for (size_t i = 0; i < sizeof(value) && offset + i < key->key_len && offset + i < 16; ++i)
		value |= (uint32_t)key->key[offset + i] << (i * 8);

	return value;
}

#define KNOWN_FLAGS (U80211_DRV_KEY_PAIRWISE | U80211_DRV_KEY_GROUP | U80211_DRV_KEY_RX | U80211_DRV_KEY_TX)

int u80211_drv_rtl8188eu_set_key(u80211_drv_rtl8188eu_t *rtl8188eu, const u80211_drv_key_t *key) {
	if (key->index > U80211_DRV_RTL8188EU_CAM_CTL0_KEY_ID_MASK)
		return U80211_DRV_STATUS_INVALID_ARGUMENT;

	bool pairwise = (key->flags & U80211_DRV_KEY_PAIRWISE) != 0;
	bool group = (key->flags & U80211_DRV_KEY_GROUP) != 0;
	if (pairwise == group || (key->flags & (U80211_DRV_KEY_RX | U80211_DRV_KEY_TX)) == 0 || (key->flags & ~KNOWN_FLAGS) != 0)
		return U80211_DRV_STATUS_INVALID_ARGUMENT;

	uint32_t algorithm;
	int status = rtl8188eu_key_parameters(key, &algorithm);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// invalidate an existing entry before changing any of its contents
	status = rtl8188eu_cam_write_word(rtl8188eu->device, key->index, 0, 0);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// clear the high words
	status = rtl8188eu_cam_write_word(rtl8188eu->device, key->index, 6, 0);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = rtl8188eu_cam_write_word(rtl8188eu->device, key->index, 7, 0);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// copy the key
	for (uint8_t word = RTL8188EU_CAM_KEY_WORD_FIRST; word <= RTL8188EU_CAM_KEY_WORD_LAST; ++word) {
		status = rtl8188eu_cam_write_word(rtl8188eu->device, key->index, word, rtl8188eu_key_word(key, (word - 2) * 4));
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;
	}

	// write the mac address high bytes
	uint32_t mac_high = key->peer[2] | ((uint32_t)key->peer[3] << 8) | ((uint32_t)key->peer[4] << 16) | ((uint32_t)key->peer[5] << 24);
	status = rtl8188eu_cam_write_word(rtl8188eu->device, key->index, 1, mac_high);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// write key id, cipher id, low mac bytes and valid bit
	uint32_t control = (key->index & U80211_DRV_RTL8188EU_CAM_CTL0_KEY_ID_MASK) |
		(algorithm << U80211_DRV_RTL8188EU_CAM_CTL0_ALGORITHM_SHIFT) |
		U80211_DRV_RTL8188EU_CAM_CTL0_VALID |
		((uint32_t)key->peer[0] << 16) |
		((uint32_t)key->peer[1] << 24);
	if (group)
		control |= U80211_DRV_RTL8188EU_CAM_CTL0_GROUP;

	return rtl8188eu_cam_write_word(rtl8188eu->device, key->index, 0, control);
}

int u80211_drv_rtl8188eu_del_key(u80211_drv_rtl8188eu_t *rtl8188eu, uint8_t index) {
	if (index > U80211_DRV_RTL8188EU_CAM_CTL0_KEY_ID_MASK)
		return U80211_DRV_STATUS_INVALID_ARGUMENT;

	return rtl8188eu_cam_write_word(rtl8188eu->device, index, 0, 0);
}
