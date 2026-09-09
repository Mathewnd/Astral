#include <stdint.h>

#include <u80211_drv/rtl8188eu.h>
#include <u80211_drv/status.h>

#define RTL8188EU_LLT_MAX_POLLS 20
#define RTL8188EU_LLT_POLL_DELAY_US 5

int u80211_drv_rtl8188eu_mac_enable_infrastructure(u80211_drv_device_handle_t device) {
	// explicitly leave the RX and TX engines disabled.
	uint16_t cr = U80211_DRV_RTL8188EU_REG_CR_HCI_TXDMA_ENABLE |
		U80211_DRV_RTL8188EU_REG_CR_HCI_RXDMA_ENABLE |
		U80211_DRV_RTL8188EU_REG_CR_TXDMA_ENABLE |
		U80211_DRV_RTL8188EU_REG_CR_RXDMA_ENABLE |
		U80211_DRV_RTL8188EU_REG_CR_PROTOCOL_ENABLE |
		U80211_DRV_RTL8188EU_REG_CR_SCHEDULE_ENABLE |
		U80211_DRV_RTL8188EU_REG_CR_SECURITY_ENABLE |
		U80211_DRV_RTL8188EU_REG_CR_CALTIMER_ENABLE;

	return u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_CR, cr);
}

int u80211_drv_rtl8188eu_mac_enable_tx_rx(u80211_drv_device_handle_t device) {
	const uint16_t enable_mask = U80211_DRV_RTL8188EU_REG_CR_MAC_TX_ENABLE | U80211_DRV_RTL8188EU_REG_CR_MAC_RX_ENABLE;
	uint16_t cr;
	int status = u80211_drv_rtl8188eu_reg_read16(device, U80211_DRV_RTL8188EU_REG_CR, &cr);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	cr |= enable_mask;
	status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_CR, cr);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read16(device, U80211_DRV_RTL8188EU_REG_CR, &cr);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	if ((cr & enable_mask) != enable_mask)
		return U80211_DRV_STATUS_FAULTY_HARDWARE;

	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_rtl8188eu_mac_disable_rx_aggregation(u80211_drv_device_handle_t device) {
	uint8_t usb_special;
	int status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_USB_SPECIAL_OPTION, &usb_special);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	usb_special &= ~U80211_DRV_RTL8188EU_REG_USB_SPECIAL_OPTION_AGG_ENABLE;
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_USB_SPECIAL_OPTION, usb_special);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint8_t trxdma_ctrl;
	status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_TRXDMA_CTRL, &trxdma_ctrl);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	trxdma_ctrl &= ~U80211_DRV_RTL8188EU_REG_TRXDMA_CTRL_RXDMA_AGG_ENABLE;
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_TRXDMA_CTRL, trxdma_ctrl);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_USB_SPECIAL_OPTION, &usb_special);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_TRXDMA_CTRL, &trxdma_ctrl);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	if ((usb_special & U80211_DRV_RTL8188EU_REG_USB_SPECIAL_OPTION_AGG_ENABLE) != 0 || (trxdma_ctrl & U80211_DRV_RTL8188EU_REG_TRXDMA_CTRL_RXDMA_AGG_ENABLE) != 0)
		return U80211_DRV_STATUS_FAULTY_HARDWARE;

	return U80211_DRV_STATUS_SUCCESS;
}

static uint16_t tx_queue_mapping(uint8_t bulk_out_endpoint_count) {
	// map wifi traffic classes onto hardware queues
	uint16_t vi_queue = bulk_out_endpoint_count >= 2 ? U80211_DRV_RTL8188EU_TRXDMA_QUEUE_NORMAL : U80211_DRV_RTL8188EU_TRXDMA_QUEUE_HIGH;
	uint16_t be_bk_queue;

	if (bulk_out_endpoint_count >= 3)
		be_bk_queue = U80211_DRV_RTL8188EU_TRXDMA_QUEUE_LOW;
	else if (bulk_out_endpoint_count == 2)
		be_bk_queue = U80211_DRV_RTL8188EU_TRXDMA_QUEUE_NORMAL;
	else
		be_bk_queue = U80211_DRV_RTL8188EU_TRXDMA_QUEUE_HIGH;

	return (U80211_DRV_RTL8188EU_TRXDMA_QUEUE_HIGH << U80211_DRV_RTL8188EU_REG_TRXDMA_CTRL_VO_SHIFT) |
		(vi_queue << U80211_DRV_RTL8188EU_REG_TRXDMA_CTRL_VI_SHIFT) |
		(be_bk_queue << U80211_DRV_RTL8188EU_REG_TRXDMA_CTRL_BE_SHIFT) |
		(be_bk_queue << U80211_DRV_RTL8188EU_REG_TRXDMA_CTRL_BK_SHIFT) |
		(U80211_DRV_RTL8188EU_TRXDMA_QUEUE_HIGH << U80211_DRV_RTL8188EU_REG_TRXDMA_CTRL_MG_SHIFT) |
		(U80211_DRV_RTL8188EU_TRXDMA_QUEUE_HIGH << U80211_DRV_RTL8188EU_REG_TRXDMA_CTRL_HI_SHIFT);
}

int u80211_drv_rtl8188eu_mac_configure_tx_queues(u80211_drv_device_handle_t device, uint8_t bulk_out_endpoint_count) {
	// configure packet buffer pages
	uint32_t high_pages = U80211_DRV_RTL8188EU_TX_PAGE_NUM_HI_PQ;
	uint32_t normal_pages = bulk_out_endpoint_count >= 2 ? U80211_DRV_RTL8188EU_TX_PAGE_NUM_NORM_PQ : 0;
	uint32_t low_pages = bulk_out_endpoint_count >= 3 ? U80211_DRV_RTL8188EU_TX_PAGE_NUM_LO_PQ : 0;
	uint32_t public_pages = U80211_DRV_RTL8188EU_TX_TOTAL_PAGE_NUM - high_pages - normal_pages - low_pages - 1;

	int status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_RQPN_NPQ, normal_pages << U80211_DRV_RTL8188EU_REG_RQPN_NPQ_SHIFT);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint32_t rqpn = U80211_DRV_RTL8188EU_REG_RQPN_LOAD |
		(high_pages << U80211_DRV_RTL8188EU_REG_RQPN_HI_SHIFT) |
		(low_pages << U80211_DRV_RTL8188EU_REG_RQPN_LO_SHIFT) |
		(public_pages << U80211_DRV_RTL8188EU_REG_RQPN_PUB_SHIFT);
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_RQPN, rqpn);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint16_t trxdma_ctrl;
	status = u80211_drv_rtl8188eu_reg_read16(device, U80211_DRV_RTL8188EU_REG_TRXDMA_CTRL, &trxdma_ctrl);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	trxdma_ctrl = (trxdma_ctrl & U80211_DRV_RTL8188EU_REG_TRXDMA_CTRL_LOW_CONTROL_MASK) | tx_queue_mapping(bulk_out_endpoint_count);
	return u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_TRXDMA_CTRL, trxdma_ctrl);
}

int u80211_drv_rtl8188eu_mac_configure_rx_fifo_boundary(u80211_drv_device_handle_t device) {
	uint16_t reg = U80211_DRV_RTL8188EU_REG_TRXFF_BNDY + 2;
	int status = u80211_drv_rtl8188eu_reg_write16(device, reg, U80211_DRV_RTL8188EU_RX_FIFO_BOUNDARY);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	uint16_t boundary;
	status = u80211_drv_rtl8188eu_reg_read16(device, reg, &boundary);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	if (boundary != U80211_DRV_RTL8188EU_RX_FIFO_BOUNDARY)
		return U80211_DRV_STATUS_FAULTY_HARDWARE;

	return U80211_DRV_STATUS_SUCCESS;
}

int u80211_drv_rtl8188eu_mac_configure_packet_buffer(u80211_drv_device_handle_t device) {
	uint8_t boundary = U80211_DRV_RTL8188EU_TX_TOTAL_PAGE_NUM + 1;
	const uint16_t boundary_regs[] = {
		U80211_DRV_RTL8188EU_REG_TXPKTBUF_BCNQ_BDNY,
		U80211_DRV_RTL8188EU_REG_TXPKTBUF_MGQ_BDNY,
		U80211_DRV_RTL8188EU_REG_TXPKTBUF_WMAC_LBK_BF_HD,
		U80211_DRV_RTL8188EU_REG_TRXFF_BNDY,
		U80211_DRV_RTL8188EU_REG_TDECTRL + 1,
	};

	for (size_t i = 0; i < sizeof(boundary_regs) / sizeof(boundary_regs[0]); ++i) {
		int status = u80211_drv_rtl8188eu_reg_write8(device, boundary_regs[i], boundary);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;
	}

	return u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_PBP, U80211_DRV_RTL8188EU_REG_PBP_128_BYTES);
}

static int rtl8188eu_llt_write(u80211_drv_device_handle_t device, uint8_t address, uint8_t data) {
	// write the LLT entry
	uint32_t command = U80211_DRV_RTL8188EU_REG_LLT_INIT_OP_WRITE |  ((uint32_t)address << 8) | data;
	int status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_LLT_INIT, command);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// wait for the write operation to complete
	for (unsigned int poll = 0; poll < RTL8188EU_LLT_MAX_POLLS; ++poll) {
		uint32_t value;
		status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_LLT_INIT, &value);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;

		if ((value & U80211_DRV_RTL8188EU_REG_LLT_INIT_OP_MASK) == 0)
			return U80211_DRV_STATUS_SUCCESS;

		u80211_drv_kernel_stall_us(RTL8188EU_LLT_POLL_DELAY_US);
	}

	return U80211_DRV_STATUS_TIMEOUT;
}

int u80211_drv_rtl8188eu_mac_initialize_llt(u80211_drv_device_handle_t device) {
	// write the linear tx page list
	for (unsigned int entry = 0; entry < U80211_DRV_RTL8188EU_TX_TOTAL_PAGE_NUM; ++entry) {
		int status = rtl8188eu_llt_write(device, (uint8_t)entry, (uint8_t)(entry + 1));
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;
	}

	int status = rtl8188eu_llt_write(device, U80211_DRV_RTL8188EU_TX_TOTAL_PAGE_NUM, U80211_DRV_RTL8188EU_LLT_END);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// write the cyclic pages
	uint8_t first_remaining_page = U80211_DRV_RTL8188EU_TX_TOTAL_PAGE_NUM + 1;
	for (unsigned int entry = first_remaining_page; entry < U80211_DRV_RTL8188EU_LLT_LAST_ENTRY; ++entry) {
		status = rtl8188eu_llt_write(device, (uint8_t)entry, (uint8_t)(entry + 1));
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;
	}

	return rtl8188eu_llt_write(device, U80211_DRV_RTL8188EU_LLT_LAST_ENTRY, first_remaining_page);
}

int u80211_drv_rtl8188eu_mac_configure_wmac(u80211_drv_rtl8188eu_t *rtl8188eu) {
	// 32-byte phy data aftter every rx descriptor
	u80211_drv_device_handle_t device = rtl8188eu->device;
	int status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_RX_DRVINFO_SZ, U80211_DRV_RTL8188EU_RX_DRVINFO_SZ);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// program the efuse mac into the wmac
	for (unsigned int i = 0; i < U80211_DRV_RTL8188EU_MAC_ADDRESS_LEN; ++i) {
		status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_MACID + i, rtl8188eu->efuse.mac_address[i]);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;
	}

	// set to 'no link' mode
	uint8_t msr;
	status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_MSR, &msr);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	msr &= ~U80211_DRV_RTL8188EU_REG_MSR_NETWORK_TYPE_MASK;
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_MSR, msr);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// configure the receive filter
	// (any frames, append phy receive status, report icv and mic information)
	uint32_t rcr = U80211_DRV_RTL8188EU_REG_RCR_AAP |
		U80211_DRV_RTL8188EU_REG_RCR_APM |
		U80211_DRV_RTL8188EU_REG_RCR_AM |
		U80211_DRV_RTL8188EU_REG_RCR_AB |
		U80211_DRV_RTL8188EU_REG_RCR_AMF |
		U80211_DRV_RTL8188EU_REG_RCR_HTC_LOC_CTRL |
		U80211_DRV_RTL8188EU_REG_RCR_APP_PHYSTS |
		U80211_DRV_RTL8188EU_REG_RCR_APP_ICV |
		U80211_DRV_RTL8188EU_REG_RCR_APP_MIC;
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_RCR, rcr);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// accept all multicasts
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_MAR, UINT32_MAX);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_MAR + 4, UINT32_MAX);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// accept management and data, reject control
	status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_RXFLTMAP0, UINT16_MAX);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_RXFLTMAP1, 0);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	return u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_RXFLTMAP2, UINT16_MAX);
}

int u80211_drv_rtl8188eu_mac_set_edca(u80211_drv_device_handle_t device, u80211_drv_rtl8188eu_access_category_t access_category, u80211_drv_rtl8188eu_edca_params_t params) {
	uint16_t reg;
	switch (access_category) {
		case U80211_DRV_RTL8188EU_ACCESS_CATEGORY_VO:
			reg = U80211_DRV_RTL8188EU_REG_EDCA_VO;
			break;
		case U80211_DRV_RTL8188EU_ACCESS_CATEGORY_VI:
			reg = U80211_DRV_RTL8188EU_REG_EDCA_VI;
			break;
		case U80211_DRV_RTL8188EU_ACCESS_CATEGORY_BE:
			reg = U80211_DRV_RTL8188EU_REG_EDCA_BE;
			break;
		case U80211_DRV_RTL8188EU_ACCESS_CATEGORY_BK:
			reg = U80211_DRV_RTL8188EU_REG_EDCA_BK;
			break;
		default:
			return U80211_DRV_STATUS_INVALID_ARGUMENT;
	}

	unsigned int aifs = params.aifsn * params.slot_time + 10;
	if (params.ecwmin > 0x0f || params.ecwmax > 0x0f || aifs > UINT8_MAX)
		return U80211_DRV_STATUS_INVALID_ARGUMENT;

	uint32_t value = ((uint32_t)params.txop << 16) | ((uint32_t)params.ecwmax << 12) | ((uint32_t)params.ecwmin << 8) | aifs;
	return u80211_drv_rtl8188eu_reg_write32(device, reg, value);
}

int u80211_drv_rtl8188eu_mac_configure_timing(u80211_drv_device_handle_t device) {
	// set fallback response rate to 1mbps using cck
	uint32_t rrsr;
	int status = u80211_drv_rtl8188eu_reg_read32(device, U80211_DRV_RTL8188EU_REG_RRSR, &rrsr);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	rrsr = (rrsr & ~U80211_DRV_RTL8188EU_REG_RRSR_RATE_MASK) | U80211_DRV_RTL8188EU_REG_RRSR_RATE_CCK_ONLY_1M;
	status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_RRSR, rrsr);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// set retry limits to 48 retries
	status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_RL, 0x3030);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// configure SIFS
	const uint16_t sifs_regs[] = {
		U80211_DRV_RTL8188EU_REG_SPEC_SIFS,
		U80211_DRV_RTL8188EU_REG_SIFS_CCK,
		U80211_DRV_RTL8188EU_REG_SIFS_OFDM,
		U80211_DRV_RTL8188EU_REG_MAC_SPEC_SIFS,
		U80211_DRV_RTL8188EU_REG_RESP_SIFS_CCK,
		U80211_DRV_RTL8188EU_REG_RESP_SIFS_OFDM,
	};
	for (size_t i = 0; i < sizeof(sifs_regs) / sizeof(sifs_regs[0]); ++i) {
		status = u80211_drv_rtl8188eu_reg_write16(device, sifs_regs[i], 0x100a);
		if (status != U80211_DRV_STATUS_SUCCESS)
			return status;
	}

	// use AMPDU new retry mechanism
	uint8_t txq_ctrl;
	status = u80211_drv_rtl8188eu_reg_read8(device, U80211_DRV_RTL8188EU_REG_FWHW_TXQ_CTRL, &txq_ctrl);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	txq_ctrl |= U80211_DRV_RTL8188EU_REG_FWHW_TXQ_CTRL_AMPDU_RTY_NEW;
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_FWHW_TXQ_CTRL, txq_ctrl);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// set ack timeout to 64 microseconds
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_ACKTO, 0x40);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// initialize beacon packet machinery
	status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_BCN_CTRL, 0x1010);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_TBTT_PROHIBIT, 0x6404);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_DRVERLYINT, 0x05);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_BCNDMATIM, 0x02);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	return u80211_drv_rtl8188eu_reg_write16(device, U80211_DRV_RTL8188EU_REG_BCNTCFG, 0x660f);
}

int u80211_drv_rtl8188eu_mac_configure_hardware_offloads(u80211_drv_device_handle_t device) {
	// configure encryption offload
	uint32_t cam_command = U80211_DRV_RTL8188EU_REG_CAMCMD_CLR | U80211_DRV_RTL8188EU_REG_CAMCMD_POLLING;
	int status = u80211_drv_rtl8188eu_reg_write32(device, U80211_DRV_RTL8188EU_REG_CAMCMD, cam_command);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// station-mode WPA keys are selected from the CAM by peer address or the
	// cipher-header key ID. the default-key bits are for legacy key selection
	// and can make encrypted data use a different CAM entry than the handshake.
	uint8_t security_config = U80211_DRV_RTL8188EU_REG_SECCFG_TXENC_ENABLE |
		U80211_DRV_RTL8188EU_REG_SECCFG_RXENC_ENABLE;
	status = u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_SECCFG, security_config);
	if (status != U80211_DRV_STATUS_SUCCESS)
		return status;

	// configure sequence handling offload
	return u80211_drv_rtl8188eu_reg_write8(device, U80211_DRV_RTL8188EU_REG_HWSEQ_CTRL, UINT8_MAX);
}
