#include <stdint.h>

#include <u80211_drv/kernel_interface.h>
#include <u80211_drv/rtl8188eu.h>
#include <u80211_drv/status.h>

#define POWER_MAX_POLLS 500
#define POWER_POLL_DELAY_US 10

int u80211_drv_rtl8188eu_power_active(u80211_drv_device_handle_t device) {
	uint16_t value16;
	int status = u80211_drv_rtl8188eu_reg_read16(device, U80211_DRV_RTL8188EU_REG_APS_FSMCO, &value16);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// disable wlan suspend and some pcie state machine stuff?
	value16 &= ~(U80211_DRV_RTL8188EU_REG_APS_FSMCO_HW_SUSPEND | U80211_DRV_RTL8188EU_REG_APS_FSMCO_PCIE);
	status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_APS_FSMCO, value16);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: waiting for power ready");

	uint32_t value32;
	unsigned int poll;
	for (poll = 0; poll < POWER_MAX_POLLS; ++poll) {
		status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_APS_FSMCO, &value32);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;

		if ((value32 & U80211_DRV_RTL8188EU_REG_APS_FSMCO_POWER_READY) != 0)
			break;

		u80211_drv_kernel_stall_us(POWER_POLL_DELAY_US);
	}

	if (poll == POWER_MAX_POLLS)
		return U80211_DRV_STATUS_TIMEOUT;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: power ready");

	// hold baseband off for now
	uint8_t value8;
	status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_SYS_FUNC, &value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value8 &= ~(U80211_DRV_RTL8188EU_REG_SYS_FUNC_BBRSTB | U80211_DRV_RTL8188EU_REG_SYS_FUNC_BB_GLB_RSTN);
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_SYS_FUNC, value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// configure analog frontend crystal
	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_AFE_XTAL_CTRL, &value32);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value32 |= U80211_DRV_RTL8188EU_REG_AFE_XTAL_CTRL_SCHMITT_TRIGGER;
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_AFE_XTAL_CTRL, value32);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// power on the device proper (disable hardware power down bit)
	status = u80211_drv_rtl8188eu_reg_read16(device, U80211_DRV_RTL8188EU_REG_APS_FSMCO, &value16);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value16 &= ~U80211_DRV_RTL8188EU_REG_APS_FSMCO_HW_POWERDOWN;
	status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_APS_FSMCO, value16);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// disable wlan suspend and pcie state machine stuff (again)
	status = u80211_drv_rtl8188eu_reg_read16(device, U80211_DRV_RTL8188EU_REG_APS_FSMCO, &value16);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value16 &= ~(U80211_DRV_RTL8188EU_REG_APS_FSMCO_HW_SUSPEND | U80211_DRV_RTL8188EU_REG_APS_FSMCO_PCIE);
	status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_APS_FSMCO, value16);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// enable MAC
	status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_APS_FSMCO, &value32);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value32 |= U80211_DRV_RTL8188EU_REG_APS_FSMCO_MAC_ENABLE;
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_APS_FSMCO, value32);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: waiting for MAC ready");
	for (poll = 0; poll < POWER_MAX_POLLS; ++poll) {
		status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_APS_FSMCO, &value32);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;

		if ((value32 & U80211_DRV_RTL8188EU_REG_APS_FSMCO_MAC_ENABLE) == 0)
			break;

		u80211_drv_kernel_stall_us(POWER_POLL_DELAY_US);
	}

	if (poll == POWER_MAX_POLLS)
		return U80211_DRV_STATUS_TIMEOUT;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "rtl8188eu: MAC ready");

	// enable low power low-dropout
	status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_LPLDO_CTRL, &value8);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	value8 &= ~U80211_DRV_RTL8188EU_REG_LPLDO_CTRL_SLEEP;
	return u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_LPLDO_CTRL, value8);
}
