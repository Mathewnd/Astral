/*	$OpenBSD: r92creg.h,v 1.31 2024/09/01 03:14:48 jsg Exp $	*/

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

#include <stddef.h>
#include <stdint.h>

#include <u80211_drv/rtl8188eu.h>
#include <u80211_drv/status.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define RTL8188EU_RF_TABLE_ENTRY_COUNT 95
#define RTL8188EU_RF_READ_DELAY_US 1000

// Imported from OpenBSD sys/dev/ic/r92creg.h.
static const uint16_t rtl8188eu_rf_regs[] = {
	0x00, 0x08, 0x18, 0x19, 0x1e, 0x1f, 0x2f, 0x3f, 0x42, 0x57, 0x58,
	0x67, 0x83, 0xb0, 0xb1, 0xb2, 0xb4, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
	0xbb, 0xbf, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca,
	0xdf, 0xef, 0x51, 0x52, 0x53, 0x56, 0x35, 0x35, 0x35, 0x36, 0x36,
	0x36, 0x36, 0xb6, 0x18, 0x5a, 0x19, 0x34, 0x34, 0x34, 0x34, 0x34,
	0x34, 0x34, 0x34, 0x34, 0x34, 0x34, 0x00, 0x84, 0x86, 0x87, 0x8e,
	0x8f, 0xef, 0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0x3b,
	0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0xef, 0x00, 0x18, 0xfe,
	0xfe, 0x1f, 0xfe, 0xfe, 0x1e, 0x1f, 0x00
};

static const uint32_t rtl8188eu_rf_values[] = {
	0x30000, 0x84000, 0x00407, 0x00012, 0x80009, 0x00880, 0x1a060,
	0x00000, 0x060c0, 0xd0000, 0xbe180, 0x01552, 0x00000, 0xff8fc,
	0x54400, 0xccc19, 0x43003, 0x4953e, 0x1c718, 0x060ff, 0x80001,
	0x40000, 0x00400, 0xc0000, 0x02400, 0x00009, 0x40c91, 0x99999,
	0x000a3, 0x88820, 0x76c06, 0x00000, 0x80000, 0x00180, 0x001a0,
	0x6b27d, 0x7e49d, 0x00073, 0x51ff3, 0x00086, 0x00186, 0x00286,
	0x01c25, 0x09c25, 0x11c25, 0x19c25, 0x48538, 0x00c07, 0x4bd00,
	0x739d0, 0x0adf3, 0x09df0, 0x08ded, 0x07dea, 0x06de7, 0x054ee,
	0x044eb, 0x034e8, 0x0246b, 0x01468, 0x0006d, 0x30159, 0x68200,
	0x000ce, 0x48a00, 0x65540, 0x88000, 0x020a0, 0xf02b0, 0xef7b0,
	0xd4fb0, 0xcf060, 0xb0090, 0xa0080, 0x90080, 0x8f780, 0x722b0,
	0x6f7b0, 0x54fb0, 0x4f060, 0x30090, 0x20080, 0x10080, 0x0f780,
	0x000a0, 0x10159, 0x0f407, 0x00000, 0x00000, 0x80003, 0x00000,
	0x00000, 0x00001, 0x80000, 0x33e60
};

int u80211_drv_rtl8188eu_rf_write(u80211_drv_device_handle_t device, uint8_t rf_reg, uint32_t value) {
	uint32_t command = ((uint32_t)rf_reg << U80211_DRV_RTL8188EU_REG_LSSI_PARAM_ADDRESS_SHIFT) | (value & U80211_DRV_RTL8188EU_REG_LSSI_PARAM_DATA_MASK);
	int status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_LSSI_PARAM_A, command);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	u80211_drv_kernel_stall_us(1);
	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_rtl8188eu_rf_read(u80211_drv_device_handle_t device, uint8_t rf_reg, uint32_t *value) {
	// drop read edge
	uint32_t hssi_param2;
	int status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_A, &hssi_param2);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_A, hssi_param2 & ~U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_READ_EDGE);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	u80211_drv_kernel_stall_us(RTL8188EU_RF_READ_DELAY_US);

	// program rf register address and raise read edge
	uint32_t command = hssi_param2 & ~U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_READ_ADDRESS_MASK;
	command |= ((uint32_t)rf_reg << U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_READ_ADDRESS_SHIFT) & U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_READ_ADDRESS_MASK;
	command |= U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_READ_EDGE;
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_A, command);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	u80211_drv_kernel_stall_us(RTL8188EU_RF_READ_DELAY_US);

	// read back
	uint32_t hssi_param1;
	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_HSSI_PARAM1_A, &hssi_param1);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint16_t readback_reg = (hssi_param1 & U80211_DRV_RTL8188EU_REG_HSSI_PARAM1_PI) != 0 ? U80211_DRV_RTL8188EU_REG_HSPI_READBACK_A : U80211_DRV_RTL8188EU_REG_LSSI_READBACK_A;
	uint32_t readback;
	status = u80211_drv_rtl8188eu_reg_read32(device, readback_reg, &readback);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	*value = readback & U80211_DRV_RTL8188EU_RF_READBACK_MASK;
	return U80211_DRV_STATUS_SUCCESS;
}

static int rtl8188eu_rf_prepare(u80211_drv_device_handle_t device) {
	uint32_t value;
	int status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_OE_A, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value |= U80211_DRV_RTL8188EU_REG_RF_IFACE_OE_RF_ENV_ENABLE;
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_OE_A, value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;
	u80211_drv_kernel_stall_us(50);

	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_OE_A, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value |= U80211_DRV_RTL8188EU_REG_RF_IFACE_OE_RF_ENV;
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_OE_A, value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;
	u80211_drv_kernel_stall_us(50);

	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_A, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value &= ~U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_ADDRESS_LENGTH;
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_A, value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;
	u80211_drv_kernel_stall_us(50);

	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_A, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value &= ~U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_DATA_LENGTH;
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_A, value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;
	u80211_drv_kernel_stall_us(50);

	return U80211_DRV_STATUS_SUCCESS;
}

static int rtl8188eu_rf_apply_table(u80211_drv_device_handle_t device) {
	for (size_t i = 0; i < ARRAY_SIZE(rtl8188eu_rf_regs); ++i) {
		uint16_t reg = rtl8188eu_rf_regs[i];
		uint32_t delay;

		switch (reg) {
			case 0xfe:
			case 0xffe:
				delay = 50000;
				break;
			case 0xfd:
				delay = 5000;
				break;
			case 0xfc:
				delay = 1000;
				break;
			case 0xfb:
				delay = 50;
				break;
			case 0xfa:
				delay = 5;
				break;
			case 0xf9:
				delay = 1;
				break;
			default: {
				int status = u80211_drv_rtl8188eu_rf_write(device, (uint8_t)reg, rtl8188eu_rf_values[i]);
				if (status != U80211_DRV_STATUS_SUCCESS) {
					u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_ERROR, "rtl8188eu: RF table write failed");
					return status;
				}

				u80211_drv_kernel_stall_us(5);
				continue;
			}
		}

		u80211_drv_kernel_stall_us(delay);
	}

	return U80211_DRV_STATUS_SUCCESS;
}

static int rtl8188eu_rf_restore_env(u80211_drv_device_handle_t device, uint32_t saved_rf_env) {
	uint32_t value;
	int status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_SW_A, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value &= ~U80211_DRV_RTL8188EU_REG_RF_IFACE_SW_RF_ENV;
	value |= saved_rf_env;
	return u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_SW_A, value);
}

int u80211_drv_rtl8188eu_rf_load_table(u80211_drv_device_handle_t device) {
	uint32_t value;
	int status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_RF_IFACE_SW_A, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint32_t saved_rf_env = value & U80211_DRV_RTL8188EU_REG_RF_IFACE_SW_RF_ENV;
	status = rtl8188eu_rf_prepare(device);
	if (status == U80211_DRV_STATUS_SUCCESS)
		status = rtl8188eu_rf_apply_table(device);

	int restore_status = rtl8188eu_rf_restore_env(device, saved_rf_env);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	return restore_status;
}
