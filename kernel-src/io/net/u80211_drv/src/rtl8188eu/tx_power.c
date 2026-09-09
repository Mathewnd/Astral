/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2015 Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2016 Nathanial Sloss <nathanialsloss@yahoo.com.au>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdint.h>

#include <u80211_drv/rtl8188eu.h>
#include <u80211_drv/status.h>

#define RTL8188EU_MAX_TX_POWER 0x3f

typedef struct {
	uint8_t cck1;
	uint8_t cck2;
	uint8_t cck55;
	uint8_t cck11;
	uint8_t ofdm;
	uint8_t mcs;
} rtl8188eu_tx_power_t;

static unsigned int rtl8188eu_channel_group(uint8_t channel) {
	if (channel <= 2)
		return 0;

	if (channel <= 5)
		return 1;

	if (channel <= 8)
		return 2;

	if (channel <= 11)
		return 3;

	if (channel <= 13)
		return 4;

	return 5;
}

static int rtl8188eu_sign_extend_4(uint8_t value) {
	value &= 0x0f;
	return (value & 0x08) != 0 ? (int)value - 16 : value;
}

static uint8_t rtl8188eu_clamp_tx_power(int value) {
	if (value < 0)
		return 0;

	if (value > RTL8188EU_MAX_TX_POWER)
		return RTL8188EU_MAX_TX_POWER;

	return value;
}

static rtl8188eu_tx_power_t rtl8188eu_compute_tx_power(const u80211_drv_rtl8188eu_efuse_t *efuse, uint8_t channel) {
	unsigned int group = rtl8188eu_channel_group(channel);
	int cck = efuse->cck_tx_power_base_indexes[group];
	unsigned int ht_group = group == 5 ? 4 : group;
	int ht = efuse->ht40_1s_tx_power_base_indexes[ht_group];
	int ofdm = ht + rtl8188eu_sign_extend_4(efuse->ht20_ofdm_tx_power_diff);
	int mcs = ht + rtl8188eu_sign_extend_4(efuse->ht20_ofdm_tx_power_diff >> 4);

	return (rtl8188eu_tx_power_t) {
		.cck1 = rtl8188eu_clamp_tx_power(cck),
		.cck2 = rtl8188eu_clamp_tx_power(cck - 9),
		.cck55 = rtl8188eu_clamp_tx_power(cck),
		.cck11 = rtl8188eu_clamp_tx_power(cck),
		.ofdm = rtl8188eu_clamp_tx_power(ofdm),
		.mcs = rtl8188eu_clamp_tx_power(mcs),
	};
}

static uint32_t rtl8188eu_repeat_power(uint8_t power) {
	return (uint32_t)power * UINT32_C(0x01010101);
}

int u80211_drv_rtl8188eu_set_tx_power(u80211_drv_rtl8188eu_t *rtl8188eu, uint8_t channel) {
	if (channel < 1 || channel > 14)
		return U80211_DRV_STATUS_INVALID_ARGUMENT;

	rtl8188eu_tx_power_t power = rtl8188eu_compute_tx_power(&rtl8188eu->efuse, channel);
	uint32_t value;
	int status = u80211_drv_rtl8188eu_reg_read32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_TXAGC_A_CCK1, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value &= ~U80211_DRV_RTL8188EU_REG_TXAGC_A_CCK1_MASK;
	value |= (uint32_t)power.cck1 << 8;
	status = u80211_drv_rtl8188eu_reg_write32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_TXAGC_A_CCK1, value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_TXAGC_B_CCK11_A_CCK2_11, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value &= UINT32_C(0x000000ff);
	value |= (uint32_t)power.cck2 << 8;
	value |= (uint32_t)power.cck55 << 16;
	value |= (uint32_t)power.cck11 << 24;
	status = u80211_drv_rtl8188eu_reg_write32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_TXAGC_B_CCK11_A_CCK2_11, value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint32_t ofdm = rtl8188eu_repeat_power(power.ofdm);
	status = u80211_drv_rtl8188eu_reg_write32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_TXAGC_A_RATE18_06, ofdm);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_TXAGC_A_RATE54_24, ofdm);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint32_t mcs = rtl8188eu_repeat_power(power.mcs);
	status = u80211_drv_rtl8188eu_reg_write32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_TXAGC_A_MCS03, mcs);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	return u80211_drv_rtl8188eu_reg_write32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_TXAGC_A_MCS07, mcs);
}
