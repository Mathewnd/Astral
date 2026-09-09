/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2015 Stefan Sperling <stsp@openbsd.org>
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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <u80211_drv/rtl8188eu.h>
#include <u80211_drv/status.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#define RTL8188EU_IQK_RUN_COUNT 3
#define RTL8188EU_IQK_TOLERANCE 5
#define RTL8188EU_IQK_DELAY_US 1000
#define RTL8188EU_LC_MAX_POLLS 100
#define RTL8188EU_LC_POLL_DELAY_US 100

#define RTL8188EU_REG_GPIO_MUXCFG 0x0040
#define RTL8188EU_REG_GPIO_MUXCFG_ENBT (1u << 5)

#define RTL8188EU_REG_TXPAUSE 0x0522
#define RTL8188EU_REG_TXPAUSE_IQK_QUEUES 0x3f
#define RTL8188EU_REG_BCN_CTRL1 0x0551
#define RTL8188EU_REG_BCN_CTRL_ENABLE (1u << 3)

#define RTL8188EU_REG_CCK0_AFESETTING 0x0a04
#define RTL8188EU_REG_OFDM1_LSTF 0x0d00
#define RTL8188EU_REG_CONFIG_ANT_A 0x0b68
#define RTL8188EU_REG_OFDM0_TRXPATHENA 0x0c04
#define RTL8188EU_REG_OFDM0_TRMUXPAR 0x0c08
#define RTL8188EU_REG_OFDM0_RXIQIMBALANCE_A 0x0c14
#define RTL8188EU_REG_OFDM0_ECCATHRESHOLD 0x0c4c
#define RTL8188EU_REG_OFDM0_TXIQIMBALANCE_A 0x0c80
#define RTL8188EU_REG_OFDM0_TXAFE_A 0x0c94
#define RTL8188EU_REG_OFDM0_RXIQEXTANTA 0x0ca0

#define RTL8188EU_REG_FPGA0_IQK 0x0e28
#define RTL8188EU_REG_TX_IQK_TONE_A 0x0e30
#define RTL8188EU_REG_RX_IQK_TONE_A 0x0e34
#define RTL8188EU_REG_TX_IQK_PI_A 0x0e38
#define RTL8188EU_REG_RX_IQK_PI_A 0x0e3c
#define RTL8188EU_REG_TX_IQK 0x0e40
#define RTL8188EU_REG_RX_IQK 0x0e44
#define RTL8188EU_REG_IQK_AGC_PTS 0x0e48
#define RTL8188EU_REG_IQK_AGC_RSP 0x0e4c
#define RTL8188EU_REG_RX_IQK_TONE_B 0x0e54
#define RTL8188EU_REG_TX_POWER_BEFORE_IQK_A 0x0e94
#define RTL8188EU_REG_TX_POWER_AFTER_IQK_A 0x0e9c
#define RTL8188EU_REG_RX_POWER_BEFORE_IQK_A 0x0ea4
#define RTL8188EU_REG_RX_POWER_AFTER_IQK_A 0x0eac

#define RTL8188EU_IQK_TX_FAILURE (1u << 28)
#define RTL8188EU_IQK_RX_FAILURE (1u << 27)
#define RTL8188EU_IQK_RESULT_MASK 0x03ff
#define RTL8188EU_IQK_TX0_FAILURE 0x0142
#define RTL8188EU_IQK_TX1_FAILURE 0x0042
#define RTL8188EU_IQK_RX0_FAILURE 0x0132
#define RTL8188EU_IQK_RX1_FAILURE 0x0036

#define RTL8188EU_RF_AC 0x00
#define RTL8188EU_RF_AC_MODE_MASK 0x70000
#define RTL8188EU_RF_AC_MODE_STANDBY 0x10000
#define RTL8188EU_RF_CHNLBW_LCSTART 0x08000
#define RTL8188EU_CONTINUOUS_TX_MASK 0x70

static const uint16_t rtl8188eu_iqk_adda_regs[] = {
	0x085c,
	0x0e6c, 0x0e70, 0x0e74,
	0x0e78, 0x0e7c, 0x0e80, 0x0e84,
	0x0e88, 0x0e8c,
	0x0ed0, 0x0ed4, 0x0ed8, 0x0edc,
	0x0ee0, 0x0eec,
};

typedef struct {
	uint32_t adda[ARRAY_SIZE(rtl8188eu_iqk_adda_regs)];
	uint8_t txpause;
	uint8_t bcn_ctrl;
	uint8_t bcn_ctrl1;
	uint32_t gpio_mux;
	uint32_t trxpathena;
	uint32_t trmuxpar;
	uint32_t rfifacesw0;
	uint32_t rfifacesw1;
	uint32_t rfifaceoe0;
	uint32_t rfifaceoe1;
	uint32_t ant_a;
	uint32_t cck_afe;
	uint8_t agc_gain;
} rtl8188eu_iqk_saved_t;

typedef struct {
	uint16_t tx[2];
	uint16_t rx[2];
	bool tx_valid;
	bool rx_valid;
} rtl8188eu_iqk_result_t;

static int rtl8188eu_iqk_save(u80211_drv_device_handle_t device, rtl8188eu_iqk_saved_t *saved) {
	// IQK reconfigures a lot of baseband stuff, so we need to save these.
	for (size_t i = 0; i < ARRAY_SIZE(rtl8188eu_iqk_adda_regs); ++i) {
		int status = u80211_drv_rtl8188eu_reg_read32(device, rtl8188eu_iqk_adda_regs[i], &saved->adda[i]);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;
	}

	int status = u80211_drv_rtl8188eu_reg_read8(device, RTL8188EU_REG_TXPAUSE, &saved->txpause);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_BCN_CTRL, &saved->bcn_ctrl);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read8(device, RTL8188EU_REG_BCN_CTRL1, &saved->bcn_ctrl1);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_GPIO_MUXCFG, &saved->gpio_mux);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_OFDM0_TRXPATHENA, &saved->trxpathena);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_OFDM0_TRMUXPAR, &saved->trmuxpar);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_SW_A, &saved->rfifacesw0);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_SW_A + 4, &saved->rfifacesw1);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_OE_A, &saved->rfifaceoe0);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_OE_A + 4, &saved->rfifaceoe1);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_CONFIG_ANT_A, &saved->ant_a);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_CCK0_AFESETTING, &saved->cck_afe);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint32_t agc;
	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_OFDM0_AGCCORE1, &agc);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	saved->agc_gain = agc & UINT8_MAX;
	return U80211_DRV_STATUS_SUCCESS;
}

static int rtl8188eu_iqk_prepare(u80211_drv_device_handle_t device, const rtl8188eu_iqk_saved_t *saved) {
	int status = u80211_drv_rtl8188eu_reg_write32(device, rtl8188eu_iqk_adda_regs[0], 0x0b1b25a0);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	for (size_t i = 1; i < ARRAY_SIZE(rtl8188eu_iqk_adda_regs); ++i) {
		status = u80211_drv_rtl8188eu_reg_write32(device, rtl8188eu_iqk_adda_regs[i], 0x0bdb25a0);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;
	}

	uint32_t hssi_param1;
	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_HSSI_PARAM1_A, &hssi_param1);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	if ((hssi_param1 & U80211_DRV_RTL8188EU_REG_HSSI_PARAM1_PI) == 0) {
		status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_HSSI_PARAM1_A, hssi_param1 | U80211_DRV_RTL8188EU_REG_HSSI_PARAM1_PI);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;
	}

	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_OFDM0_TRXPATHENA, 0x03a05600);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_OFDM0_TRMUXPAR, 0x000800e4);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_SW_A + 4, 0x22204000);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write8(device, RTL8188EU_REG_TXPAUSE, RTL8188EU_REG_TXPAUSE_IQK_QUEUES);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_BCN_CTRL, saved->bcn_ctrl & ~RTL8188EU_REG_BCN_CTRL_ENABLE);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write8(device, RTL8188EU_REG_BCN_CTRL1, saved->bcn_ctrl1 & ~RTL8188EU_REG_BCN_CTRL_ENABLE);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write8(device, RTL8188EU_REG_GPIO_MUXCFG, (uint8_t)saved->gpio_mux & ~RTL8188EU_REG_GPIO_MUXCFG_ENBT);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_CONFIG_ANT_A, 0x00080000);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_FPGA0_IQK, 0x80800000);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_TX_IQK, 0x01007c00);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	return u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_RX_IQK, 0x01004800);
}

static int rtl8188eu_iqk_measure_once(u80211_drv_device_handle_t device, rtl8188eu_iqk_result_t *result) {
	result->tx_valid = false;
	result->rx_valid = false;

	int status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_TX_IQK_TONE_A, 0x10008c1f);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_RX_IQK_TONE_B, 0x10008c1f);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_TX_IQK_PI_A, 0x82140102);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_RX_IQK_PI_A, 0x28160502);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_IQK_AGC_RSP, 0x00462911);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_IQK_AGC_PTS, 0xf9000000);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_IQK_AGC_PTS, 0xf8000000);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	u80211_drv_kernel_stall_us(RTL8188EU_IQK_DELAY_US);

	uint32_t iqk_status;
	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_RX_POWER_AFTER_IQK_A, &iqk_status);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	if ((iqk_status & RTL8188EU_IQK_TX_FAILURE) != 0)
		return U80211_DRV_STATUS_SUCCESS;

	uint32_t value;
	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_TX_POWER_BEFORE_IQK_A, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	result->tx[0] = (value >> 16) & RTL8188EU_IQK_RESULT_MASK;
	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_TX_POWER_AFTER_IQK_A, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	result->tx[1] = (value >> 16) & RTL8188EU_IQK_RESULT_MASK;
	if (result->tx[0] == RTL8188EU_IQK_TX0_FAILURE || result->tx[1] == RTL8188EU_IQK_TX1_FAILURE)
		return U80211_DRV_STATUS_SUCCESS;

	result->tx_valid = true;

	if ((iqk_status & RTL8188EU_IQK_RX_FAILURE) != 0)
		return U80211_DRV_STATUS_SUCCESS;

	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_RX_POWER_BEFORE_IQK_A, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	result->rx[0] = (value >> 16) & RTL8188EU_IQK_RESULT_MASK;
	result->rx[1] = (iqk_status >> 16) & RTL8188EU_IQK_RESULT_MASK;
	if (result->rx[0] == RTL8188EU_IQK_RX0_FAILURE || result->rx[1] == RTL8188EU_IQK_RX1_FAILURE)
		return U80211_DRV_STATUS_SUCCESS;

	result->rx_valid = true;
	return U80211_DRV_STATUS_SUCCESS;
}

static void rtl8188eu_iqk_record_error(int *result, int status) {
	if (*result == U80211_DRV_STATUS_SUCCESS && status != U80211_DRV_STATUS_SUCCESS)
		*result = status;
}

static int rtl8188eu_iqk_leave_mode(u80211_drv_device_handle_t device) {
	int result = U80211_DRV_STATUS_SUCCESS;
	int status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_FPGA0_IQK, 0);
	rtl8188eu_iqk_record_error(&result, status);
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_LSSI_PARAM_A, 0x00032ed3);
	rtl8188eu_iqk_record_error(&result, status);
	return result;
}

static int rtl8188eu_iqk_restore(u80211_drv_device_handle_t device, const rtl8188eu_iqk_saved_t *saved) {
	int result = rtl8188eu_iqk_leave_mode(device);
	int status;

	for (size_t i = 0; i < ARRAY_SIZE(rtl8188eu_iqk_adda_regs); ++i) {
		status = u80211_drv_rtl8188eu_reg_write32(device, rtl8188eu_iqk_adda_regs[i], saved->adda[i]);
		rtl8188eu_iqk_record_error(&result, status);
	}
	status = u80211_drv_rtl8188eu_reg_write8(device, RTL8188EU_REG_TXPAUSE, saved->txpause);
	rtl8188eu_iqk_record_error(&result, status);
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_BCN_CTRL, saved->bcn_ctrl);
	rtl8188eu_iqk_record_error(&result, status);
	status = u80211_drv_rtl8188eu_reg_write8(device, RTL8188EU_REG_BCN_CTRL1, saved->bcn_ctrl1);
	rtl8188eu_iqk_record_error(&result, status);
	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_GPIO_MUXCFG, saved->gpio_mux);
	rtl8188eu_iqk_record_error(&result, status);
	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_OFDM0_TRXPATHENA, saved->trxpathena);
	rtl8188eu_iqk_record_error(&result, status);
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_SW_A, saved->rfifacesw0);
	rtl8188eu_iqk_record_error(&result, status);
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_SW_A + 4, saved->rfifacesw1);
	rtl8188eu_iqk_record_error(&result, status);
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_OE_A, saved->rfifaceoe0);
	rtl8188eu_iqk_record_error(&result, status);
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_OE_A + 4, saved->rfifaceoe1);
	rtl8188eu_iqk_record_error(&result, status);
	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_OFDM0_TRMUXPAR, saved->trmuxpar);
	rtl8188eu_iqk_record_error(&result, status);
	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_CONFIG_ANT_A, saved->ant_a);
	rtl8188eu_iqk_record_error(&result, status);
	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_CCK0_AFESETTING, saved->cck_afe);
	rtl8188eu_iqk_record_error(&result, status);

	uint32_t agc;
	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_OFDM0_AGCCORE1, &agc);
	rtl8188eu_iqk_record_error(&result, status);
	if (status == U80211_DRV_STATUS_SUCCESS) {
		agc &= ~UINT32_C(0xff);
		status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_OFDM0_AGCCORE1, agc | 0x50);
		rtl8188eu_iqk_record_error(&result, status);
		status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_OFDM0_AGCCORE1, agc | saved->agc_gain);
		rtl8188eu_iqk_record_error(&result, status);
	}
	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_TX_IQK_TONE_A, 0x01008c00);
	rtl8188eu_iqk_record_error(&result, status);
	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_RX_IQK_TONE_A, 0x01008c00);
	rtl8188eu_iqk_record_error(&result, status);
	return result;
}

static unsigned int rtl8188eu_iqk_difference(uint16_t a, uint16_t b) {
	return a > b ? a - b : b - a;
}

static bool rtl8188eu_iqk_results_close(const rtl8188eu_iqk_result_t *a, const rtl8188eu_iqk_result_t *b) {
	if (!a->tx_valid || !a->rx_valid || !b->tx_valid || !b->rx_valid)
		return false;

	return rtl8188eu_iqk_difference(a->tx[0], b->tx[0]) <= RTL8188EU_IQK_TOLERANCE &&
		rtl8188eu_iqk_difference(a->tx[1], b->tx[1]) <= RTL8188EU_IQK_TOLERANCE &&
		rtl8188eu_iqk_difference(a->rx[0], b->rx[0]) <= RTL8188EU_IQK_TOLERANCE &&
		rtl8188eu_iqk_difference(a->rx[1], b->rx[1]) <= RTL8188EU_IQK_TOLERANCE;
}

static int32_t rtl8188eu_iqk_sign_extend_10(uint16_t value) {
	return (value & 0x0200) != 0 ? (int32_t)value - 0x0400 : value;
}

static int32_t rtl8188eu_iqk_arithmetic_shift(int32_t value, unsigned int shift) {
	if (value >= 0)
		return value >> shift;

	return -(((-value) + ((1u << shift) - 1)) >> shift);
}

static int rtl8188eu_iqk_write_results(u80211_drv_device_handle_t device, const rtl8188eu_iqk_result_t *result) {
	uint32_t reg;
	int status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_OFDM0_TXIQIMBALANCE_A, &reg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint32_t factor = (reg >> 22) & RTL8188EU_IQK_RESULT_MASK;
	int32_t x_product = rtl8188eu_iqk_sign_extend_10(result->tx[0]) * (int32_t)factor;
	int32_t tx_a = rtl8188eu_iqk_arithmetic_shift(x_product, 8);

	reg = (reg & ~UINT32_C(0x000003ff)) | ((uint32_t)tx_a & 0x000003ff);
	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_OFDM0_TXIQIMBALANCE_A, reg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_OFDM0_ECCATHRESHOLD, &reg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	if (((uint32_t)rtl8188eu_iqk_arithmetic_shift(x_product, 7) & 1) != 0)
		reg |= UINT32_C(0x80000000);
	else
		reg &= ~UINT32_C(0x80000000);

	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_OFDM0_ECCATHRESHOLD, reg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	int32_t y_product = rtl8188eu_iqk_sign_extend_10(result->tx[1]) * (int32_t)factor;
	int32_t tx_c = rtl8188eu_iqk_arithmetic_shift(y_product, 8);

	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_OFDM0_TXAFE_A, &reg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	reg = (reg & ~UINT32_C(0xf0000000)) | (((uint32_t)tx_c & 0x000003c0) << 22);
	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_OFDM0_TXAFE_A, reg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_OFDM0_TXIQIMBALANCE_A, &reg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	reg = (reg & ~UINT32_C(0x003f0000)) | (((uint32_t)tx_c & 0x0000003f) << 16);
	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_OFDM0_TXIQIMBALANCE_A, reg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_OFDM0_ECCATHRESHOLD, &reg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	if (((uint32_t)rtl8188eu_iqk_arithmetic_shift(y_product, 7) & 1) != 0)
		reg |= UINT32_C(0x20000000);
	else
		reg &= ~UINT32_C(0x20000000);

	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_OFDM0_ECCATHRESHOLD, reg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_OFDM0_RXIQIMBALANCE_A, &reg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	reg = (reg & ~UINT32_C(0x000003ff)) | (result->rx[0] & 0x03ff);
	reg = (reg & ~UINT32_C(0x0000fc00)) | ((uint32_t)(result->rx[1] & 0x003f) << 10);
	status = u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_OFDM0_RXIQIMBALANCE_A, reg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(device, RTL8188EU_REG_OFDM0_RXIQEXTANTA, &reg);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	reg = (reg & ~UINT32_C(0xf0000000)) | ((uint32_t)(result->rx[1] & 0x03c0) << 22);
	return u80211_drv_rtl8188eu_reg_write32(device, RTL8188EU_REG_OFDM0_RXIQEXTANTA, reg);
}

static int rtl8188eu_iq_calibrate(u80211_drv_device_handle_t device) {
	rtl8188eu_iqk_saved_t saved;
	int status = rtl8188eu_iqk_save(device, &saved);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	rtl8188eu_iqk_result_t results[RTL8188EU_IQK_RUN_COUNT];
	bool hardware_modified = false;
	bool stable = false;
	unsigned int accepted_run = 0;

	for (unsigned int run = 0; run < RTL8188EU_IQK_RUN_COUNT; ++run) {
		hardware_modified = true;
		status = rtl8188eu_iqk_prepare(device, &saved);
		if (status != U80211_DRV_STATUS_SUCCESS)
			break;

		rtl8188eu_iqk_result_t discarded;
		status = rtl8188eu_iqk_measure_once(device, &discarded);
		if (status != U80211_DRV_STATUS_SUCCESS)
			break;
		status = rtl8188eu_iqk_measure_once(device, &results[run]);
		if (status != U80211_DRV_STATUS_SUCCESS)
			break;
		status = rtl8188eu_iqk_leave_mode(device);
		if (status != U80211_DRV_STATUS_SUCCESS)
			break;

		if (run == 0)
			continue;

		int restore_status = rtl8188eu_iqk_restore(device, &saved);
		hardware_modified = false;
		if (restore_status != U80211_DRV_STATUS_SUCCESS) {
			status = restore_status;
			break;
		}

		if (rtl8188eu_iqk_results_close(&results[run - 1], &results[run])) {
			stable = true;
			accepted_run = run;
			break;
		}
	}

	if (hardware_modified) {
		int restore_status = rtl8188eu_iqk_restore(device, &saved);
		if (status == U80211_DRV_STATUS_SUCCESS)
			status = restore_status;
	}
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	if (!stable) {
		u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_WARN, "rtl8188eu: IQ calibration did not stabilize");
		return U80211_DRV_STATUS_SUCCESS;
	}

	return rtl8188eu_iqk_write_results(device, &results[accepted_run]);
}

static int rtl8188eu_lc_calibrate(u80211_drv_device_handle_t device) {
	uint8_t txmode;
	int status = u80211_drv_rtl8188eu_reg_read8(device, RTL8188EU_REG_OFDM1_LSTF + 3, &txmode);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	int result = U80211_DRV_STATUS_SUCCESS;
	bool continuous_tx = (txmode & RTL8188EU_CONTINUOUS_TX_MASK) != 0;
	bool txmode_modified = false;
	bool txpause_modified = false;
	bool rf_ac_modified = false;
	uint8_t txpause = 0;
	uint32_t rf_ac = 0;

	if (continuous_tx) {
		// disable continuous tx
		txmode_modified = true;
		status = u80211_drv_rtl8188eu_reg_write8(device, RTL8188EU_REG_OFDM1_LSTF + 3, txmode & ~RTL8188EU_CONTINUOUS_TX_MASK);
		if (status != U80211_DRV_STATUS_SUCCESS) {
			result = status;
			goto restore;
		}

		status = u80211_drv_rtl8188eu_rf_read(device, RTL8188EU_RF_AC, &rf_ac);
		if (status != U80211_DRV_STATUS_SUCCESS) {
			result = status;
			goto restore;
		}
		rf_ac_modified = true;
		status = u80211_drv_rtl8188eu_rf_write(device, RTL8188EU_RF_AC, (rf_ac & ~RTL8188EU_RF_AC_MODE_MASK) | RTL8188EU_RF_AC_MODE_STANDBY);
		if (status != U80211_DRV_STATUS_SUCCESS) {
			result = status;
			goto restore;
		}
	} else {
		// pause tx
		status = u80211_drv_rtl8188eu_reg_read8(device, RTL8188EU_REG_TXPAUSE, &txpause);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;

		txpause_modified = true;
		status = u80211_drv_rtl8188eu_reg_write8(device, RTL8188EU_REG_TXPAUSE, UINT8_MAX);
		if (status != U80211_DRV_STATUS_SUCCESS) {
			result = status;
			goto restore;
		}
	}

	// calibrate lc
	uint32_t chnlbw;
	status = u80211_drv_rtl8188eu_rf_read(device, U80211_DRV_RTL8188EU_RF_CHNLBW, &chnlbw);
	if (status != U80211_DRV_STATUS_SUCCESS) {
		result = status;
		goto restore;
	}
	status = u80211_drv_rtl8188eu_rf_write(
		device,
		U80211_DRV_RTL8188EU_RF_CHNLBW,
		chnlbw | RTL8188EU_RF_CHNLBW_LCSTART
	);
	if (status != U80211_DRV_STATUS_SUCCESS) {
		result = status;
		goto restore;
	}

	unsigned int poll;
	for (poll = 0; poll < RTL8188EU_LC_MAX_POLLS; ++poll) {
		status = u80211_drv_rtl8188eu_rf_read(device, U80211_DRV_RTL8188EU_RF_CHNLBW, &chnlbw);
		if (status != U80211_DRV_STATUS_SUCCESS) {
			result = status;
			goto restore;
		}
		if ((chnlbw & RTL8188EU_RF_CHNLBW_LCSTART) == 0)
			break;

		u80211_drv_kernel_stall_us(RTL8188EU_LC_POLL_DELAY_US);
	}
	if (poll == RTL8188EU_LC_MAX_POLLS)
		result = U80211_DRV_STATUS_TIMEOUT;

restore:
	if (rf_ac_modified) {
		status = u80211_drv_rtl8188eu_rf_write(device, RTL8188EU_RF_AC, rf_ac);
		rtl8188eu_iqk_record_error(&result, status);
	}
	if (txmode_modified) {
		status = u80211_drv_rtl8188eu_reg_write8(device, RTL8188EU_REG_OFDM1_LSTF + 3, txmode);
		rtl8188eu_iqk_record_error(&result, status);
	}
	if (txpause_modified) {
		status = u80211_drv_rtl8188eu_reg_write8(device, RTL8188EU_REG_TXPAUSE, txpause);
		rtl8188eu_iqk_record_error(&result, status);
	}
	return result;
}

static int rtl8188eu_calibration_gpio_cleanup(u80211_drv_device_handle_t device) {
	uint8_t gpio;
	int status = u80211_drv_rtl8188eu_reg_read8(device, RTL8188EU_REG_GPIO_MUXCFG, &gpio);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	return u80211_drv_rtl8188eu_reg_write8(device, RTL8188EU_REG_GPIO_MUXCFG, gpio & ~RTL8188EU_REG_GPIO_MUXCFG_ENBT);
}

int u80211_drv_rtl8188eu_calibrate(u80211_drv_device_handle_t device) {
	int status = rtl8188eu_iq_calibrate(device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = rtl8188eu_lc_calibrate(device);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	return rtl8188eu_calibration_gpio_cleanup(device);
}
