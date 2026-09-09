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
#define RTL8188EU_AGC_TABLE_ENTRY_COUNT 128

// Imported from OpenBSD sys/dev/ic/r92creg.h.
static const uint16_t rtl8188eu_bb_regs[] = {
	0x800, 0x804, 0x808, 0x80c, 0x810, 0x814, 0x818, 0x81c, 0x820,
	0x824, 0x828, 0x82c, 0x830, 0x834, 0x838, 0x83c, 0x840, 0x844,
	0x848, 0x84c, 0x850, 0x854, 0x858, 0x85c, 0x860, 0x864, 0x868,
	0x86c, 0x870, 0x874, 0x878, 0x87c, 0x880, 0x884, 0x888, 0x88c,
	0x890, 0x894, 0x898, 0x89c, 0x900, 0x904, 0x908, 0x90c, 0x910,
	0x914, 0xa00, 0xa04, 0xa08, 0xa0c, 0xa10, 0xa14, 0xa18, 0xa1c,
	0xa20, 0xa24, 0xa28, 0xa2c, 0xa70, 0xa74, 0xa78, 0xa7c, 0xa80,
	0xb2c, 0xc00, 0xc04, 0xc08, 0xc0c, 0xc10, 0xc14, 0xc18, 0xc1c,
	0xc20, 0xc24, 0xc28, 0xc2c, 0xc30, 0xc34, 0xc38, 0xc3c, 0xc40,
	0xc44, 0xc48, 0xc4c, 0xc50, 0xc54, 0xc58, 0xc5c, 0xc60, 0xc64,
	0xc68, 0xc6c, 0xc70, 0xc74, 0xc78, 0xc7c, 0xc80, 0xc84, 0xc88,
	0xc8c, 0xc90, 0xc94, 0xc98, 0xc9c, 0xca0, 0xca4, 0xca8, 0xcac,
	0xcb0, 0xcb4, 0xcb8, 0xcbc, 0xcc0, 0xcc4, 0xcc8, 0xccc, 0xcd0,
	0xcd4, 0xcd8, 0xcdc, 0xce0, 0xce4, 0xce8, 0xcec, 0xd00, 0xd04,
	0xd08, 0xd0c, 0xd10, 0xd14, 0xd18, 0xd2c, 0xd30, 0xd34, 0xd38,
	0xd3c, 0xd40, 0xd44, 0xd48, 0xd4c, 0xd50, 0xd54, 0xd58, 0xd5c,
	0xd60, 0xd64, 0xd68, 0xd6c, 0xd70, 0xd74, 0xd78, 0xe00, 0xe04,
	0xe08, 0xe10, 0xe14, 0xe18, 0xe1c, 0xe28, 0xe30, 0xe34, 0xe38,
	0xe3c, 0xe40, 0xe44, 0xe48, 0xe4c, 0xe50, 0xe54, 0xe58, 0xe5c,
	0xe60, 0xe68, 0xe6c, 0xe70, 0xe74, 0xe78, 0xe7c, 0xe80, 0xe84,
	0xe88, 0xe8c, 0xed0, 0xed4, 0xed8, 0xedc, 0xee0, 0xee8, 0xeec,
	0xf14, 0xf4c, 0xf00
};

static const uint32_t rtl8188eu_bb_values[] = {
	0x80040000, 0x00000003, 0x0000fc00, 0x0000000a, 0x10001331,
	0x020c3d10, 0x02200385, 0x00000000, 0x01000100, 0x00390204,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00010000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x569a11a9, 0x01000014, 0x66f60110,
	0x061f0649, 0x00000000, 0x27272700, 0x07000760, 0x25004000,
	0x00000808, 0x00000000, 0xb0000c1c, 0x00000001, 0x00000000,
	0xccc000c0, 0x00000800, 0xfffffffe, 0x40302010, 0x00706050,
	0x00000000, 0x00000023, 0x00000000, 0x81121111, 0x00000002,
	0x00000201, 0x00d047c8, 0x80ff000c, 0x8c838300, 0x2e7f120f,
	0x9500bb78, 0x1114d028, 0x00881117, 0x89140f00, 0x1a1b0000,
	0x090e1317, 0x00000204, 0x00d30000, 0x101fbf00, 0x00000007,
	0x00000900, 0x225b0606, 0x218075b1, 0x80000000, 0x48071d40,
	0x03a05611, 0x000000e4, 0x6c6c6c6c, 0x08800000, 0x40000100,
	0x08800000, 0x40000100, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x69e9ac47, 0x469652af, 0x49795994, 0x0a97971c,
	0x1f7c403f, 0x000100b7, 0xec020107, 0x007f037f, 0x69553420,
	0x43bc0094, 0x00013169, 0x00250492, 0x00000000, 0x7112848b,
	0x47c00bff, 0x00000036, 0x2c7f000d, 0x020610db, 0x0000001f,
	0x00b91612, 0x390000e4, 0x20f60000, 0x40000100, 0x20200000,
	0x00091521, 0x00000000, 0x00121820, 0x00007f7f, 0x00000000,
	0x000300a0, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x28000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x64b22427, 0x00766932,
	0x00222222, 0x00000000, 0x37644302, 0x2f97d40c, 0x00000740,
	0x00020401, 0x0000907f, 0x20010201, 0xa0633333, 0x3333bc43,
	0x7a8f5b6f, 0xcc979975, 0x00000000, 0x80608000, 0x00000000,
	0x00127353, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x6437140a, 0x00000000, 0x00000282, 0x30032064, 0x4653de68,
	0x04518a3c, 0x00002101, 0x2a201c16, 0x1812362e, 0x322c2220,
	0x000e3c24, 0x2d2d2d2d, 0x2d2d2d2d, 0x0390272d, 0x2d2d2d2d,
	0x2d2d2d2d, 0x2d2d2d2d, 0x2d2d2d2d, 0x00000000, 0x1000dc1f,
	0x10008c1f, 0x02140102, 0x681604c2, 0x01007c00, 0x01004800,
	0xfb000000, 0x000028d1, 0x1000dc1f, 0x10008c1f, 0x02140102,
	0x28160d05, 0x00000008, 0x001b25a4, 0x00c00014, 0x00c00014,
	0x01000014, 0x01000014, 0x01000014, 0x01000014, 0x00c00014,
	0x01000014, 0x00c00014, 0x00c00014, 0x00c00014, 0x00c00014,
	0x00000014, 0x00000014, 0x21555448, 0x01c00014, 0x00000003,
	0x00000000, 0x00000300
};

static const uint32_t rtl8188eu_agc_values[] = {
	0xfb000001, 0xfb010001, 0xfb020001, 0xfb030001, 0xfb040001,
	0xfb050001, 0xfa060001, 0xf9070001, 0xf8080001, 0xf7090001,
	0xf60a0001, 0xf50b0001, 0xf40c0001, 0xf30d0001, 0xf20e0001,
	0xf10f0001, 0xf0100001, 0xef110001, 0xee120001, 0xed130001,
	0xec140001, 0xeb150001, 0xea160001, 0xe9170001, 0xe8180001,
	0xe7190001, 0xe61a0001, 0xe51b0001, 0xe41c0001, 0xe31d0001,
	0xe21e0001, 0xe11f0001, 0x8a200001, 0x89210001, 0x88220001,
	0x87230001, 0x86240001, 0x85250001, 0x84260001, 0x83270001,
	0x82280001, 0x6b290001, 0x6a2a0001, 0x692b0001, 0x682c0001,
	0x672d0001, 0x662e0001, 0x652f0001, 0x64300001, 0x63310001,
	0x62320001, 0x61330001, 0x46340001, 0x45350001, 0x44360001,
	0x43370001, 0x42380001, 0x41390001, 0x403a0001, 0x403b0001,
	0x403c0001, 0x403d0001, 0x403e0001, 0x403f0001, 0xfb400001,
	0xfb410001, 0xfb420001, 0xfb430001, 0xfb440001, 0xfb450001,
	0xfb460001, 0xfb470001, 0xfb480001, 0xfa490001, 0xf94a0001,
	0xf84b0001, 0xf74c0001, 0xf64d0001, 0xf54e0001, 0xf44f0001,
	0xf3500001, 0xf2510001, 0xf1520001, 0xf0530001, 0xef540001,
	0xee550001, 0xed560001, 0xec570001, 0xeb580001, 0xea590001,
	0xe95a0001, 0xe85b0001, 0xe75c0001, 0xe65d0001, 0xe55e0001,
	0xe45f0001, 0xe3600001, 0xe2610001, 0xc3620001, 0xc2630001,
	0xc1640001, 0x8b650001, 0x8a660001, 0x89670001, 0x88680001,
	0x87690001, 0x866a0001, 0x856b0001, 0x846c0001, 0x676d0001,
	0x666e0001, 0x656f0001, 0x64700001, 0x63710001, 0x62720001,
	0x61730001, 0x60740001, 0x46750001, 0x45760001, 0x44770001,
	0x43780001, 0x42790001, 0x417a0001, 0x407b0001, 0x407c0001,
	0x407d0001, 0x407e0001, 0x407f0001
};

int u80211_drv_rtl8188eu_bb_enable(u80211_drv_device_handle_t device) {
	uint16_t value;
	int status = u80211_drv_rtl8188eu_reg_read16(device, U80211_DRV_RTL8188EU_REG_SYS_FUNC, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value |= U80211_DRV_RTL8188EU_REG_SYS_FUNC_BBRSTB |
		U80211_DRV_RTL8188EU_REG_SYS_FUNC_BB_GLB_RSTN |
		U80211_DRV_RTL8188EU_REG_SYS_FUNC_DIO_RF;
	status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_SYS_FUNC, value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write8(
		device,
		U80211_DRV_RTL8188EU_REG_RF_CTRL,
		U80211_DRV_RTL8188EU_REG_RF_CTRL_ENABLE |
			U80211_DRV_RTL8188EU_REG_RF_CTRL_RSTB |
			U80211_DRV_RTL8188EU_REG_RF_CTRL_SDMRSTB
	);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	return u80211_drv_rtl8188eu_reg_write8(
		device,
		U80211_DRV_RTL8188EU_REG_SYS_FUNC,
		U80211_DRV_RTL8188EU_REG_SYS_FUNC_USBA |
			U80211_DRV_RTL8188EU_REG_SYS_FUNC_USBD |
			U80211_DRV_RTL8188EU_REG_SYS_FUNC_BB_GLB_RSTN |
			U80211_DRV_RTL8188EU_REG_SYS_FUNC_BBRSTB
	);
}

int u80211_drv_rtl8188eu_bb_enable_datapaths(u80211_drv_device_handle_t device) {
	// enable cck datapath
	uint32_t rfmod;
	int status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_FPGA0_RFMOD, &rfmod);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	rfmod |= U80211_DRV_RTL8188EU_REG_FPGA0_RFMOD_CCK_ENABLE;
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_FPGA0_RFMOD, rfmod);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// enable ofdm data path
	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_FPGA0_RFMOD, &rfmod);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	rfmod |= U80211_DRV_RTL8188EU_REG_FPGA0_RFMOD_OFDM_ENABLE;
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_FPGA0_RFMOD, rfmod);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// sanity check
	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_FPGA0_RFMOD, &rfmod);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint32_t enable_mask = U80211_DRV_RTL8188EU_REG_FPGA0_RFMOD_CCK_ENABLE | U80211_DRV_RTL8188EU_REG_FPGA0_RFMOD_OFDM_ENABLE;
	if ((rfmod & enable_mask) != enable_mask)
		return U80211_DRV_STATUS_FAULTY_HARDWARE;

	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_rtl8188eu_bb_load_table(u80211_drv_device_handle_t device) {
	for (size_t i = 0; i < ARRAY_SIZE(rtl8188eu_bb_regs); ++i) {
		int status = u80211_drv_rtl8188eu_reg_write32(device, rtl8188eu_bb_regs[i], rtl8188eu_bb_values[i]);
		if (status != U80211_DRV_STATUS_SUCCESS) {
			u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_ERROR, "rtl8188eu: BB table write failed");
			return status;
		}

		u80211_drv_kernel_stall_us(1);
	}

	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_rtl8188eu_bb_load_agc_table(u80211_drv_device_handle_t device) {
	for (size_t i = 0; i < ARRAY_SIZE(rtl8188eu_agc_values); ++i) {
		int status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_OFDM0_AGCRSSITABLE, rtl8188eu_agc_values[i]);
		if (status != U80211_DRV_STATUS_SUCCESS) {
			u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_ERROR, "rtl8188eu: AGC table write failed");
			return status;
		}

		u80211_drv_kernel_stall_us(1);
	}

	int status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_OFDM0_AGCCORE1, U80211_DRV_RTL8188EU_REG_OFDM0_AGCCORE1_LATCH);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	u80211_drv_kernel_stall_us(1);

	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_OFDM0_AGCCORE1, U80211_DRV_RTL8188EU_REG_OFDM0_AGCCORE1_FINAL);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	u80211_drv_kernel_stall_us(1);
	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_rtl8188eu_bb_apply_efuse_calibration(u80211_drv_rtl8188eu_t *rtl8188eu) {
	uint32_t value;
	int status = u80211_drv_rtl8188eu_reg_read32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_AFE_XTAL_CTRL, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint32_t crystal_cap = rtl8188eu->efuse.xtal_k;
	uint32_t crystal_field = crystal_cap | (crystal_cap << 6);
	value &= ~U80211_DRV_RTL8188EU_REG_AFE_XTAL_CTRL_ADDRESS_MASK;
	value |= (crystal_field << U80211_DRV_RTL8188EU_REG_AFE_XTAL_CTRL_ADDRESS_SHIFT) & U80211_DRV_RTL8188EU_REG_AFE_XTAL_CTRL_ADDRESS_MASK;
	status = u80211_drv_rtl8188eu_reg_write32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_AFE_XTAL_CTRL, value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_A, &value);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	rtl8188eu->cck_high_power = (value & U80211_DRV_RTL8188EU_REG_HSSI_PARAM2_CCK_HIGH_POWER) != 0;
	return U80211_DRV_STATUS_SUCCESS;
}
