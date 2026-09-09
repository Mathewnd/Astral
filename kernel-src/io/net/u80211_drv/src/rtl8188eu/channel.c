#include <stdint.h>

#include <u80211_drv/rtl8188eu.h>
#include <u80211_drv/status.h>

// TODO: serialize channel setting (either here or in u80211)
int u80211_drv_rtl8188eu_set_channel(u80211_drv_rtl8188eu_t *rtl8188eu, uint8_t channel) {
	if (channel < 1 || channel > 11)
		return U80211_DRV_STATUS_INVALID_ARGUMENT;

	// set the transmission power for this channel
	int status = u80211_drv_rtl8188eu_set_tx_power(rtl8188eu, channel);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// tell wmac to use 20 mhz-sized channels
	uint8_t bwopmode;
	status = u80211_drv_rtl8188eu_reg_read8(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_BWOPMODE, &bwopmode);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	bwopmode |= U80211_DRV_RTL8188EU_REG_BWOPMODE_20MHZ;
	status = u80211_drv_rtl8188eu_reg_write8(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_BWOPMODE, bwopmode);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// tell baseband blocks we are using 20 mhz channels
	uint32_t rfmod;
	status = u80211_drv_rtl8188eu_reg_read32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_FPGA0_RFMOD, &rfmod);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	rfmod &= ~U80211_DRV_RTL8188EU_REG_FPGA_RFMOD_40MHZ;
	status = u80211_drv_rtl8188eu_reg_write32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_FPGA0_RFMOD, rfmod);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_FPGA1_RFMOD, &rfmod);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	rfmod &= ~U80211_DRV_RTL8188EU_REG_FPGA_RFMOD_40MHZ;
	status = u80211_drv_rtl8188eu_reg_write32(rtl8188eu->device, U80211_DRV_RTL8188EU_REG_FPGA1_RFMOD, rfmod);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// program channel and 20mhz bandwidth
	uint32_t chnlbw = rtl8188eu->rf_chnlbw & ~U80211_DRV_RTL8188EU_RF_CHNLBW_CHANNEL_BW_MASK;
	chnlbw |= U80211_DRV_RTL8188EU_RF_CHNLBW_BW20 | channel;
	return u80211_drv_rtl8188eu_rf_write(rtl8188eu->device, U80211_DRV_RTL8188EU_RF_CHNLBW, chnlbw);
}
