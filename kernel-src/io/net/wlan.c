#include <kernel/wlan.h>
#include <u80211/u80211.h>
#include <u80211_drv/u80211_drv.h>
#include <kernel/net.h>
#include <kernel/eth.h>
#include <kernel/interrupt.h>
#include <kernel/usercopy.h>
#include <logging.h>
#include <string.h>
#include <errno.h>

#define WLAN_MTU 1500

typedef struct {
	netdev_t netdev;
	void *driver_handle;
	u80211_device_t *u80211_device;
	const u80211_drv_device_ops_t *ops;
	int id;
} wlan_device_t;

// TODO: make u80211 bss cache get more generic than having to pass the bss cache directly
// TODO: handle hotplug
// TODO: in u80211_drv *_tx_buffer ops, pass device handle
// TODO: change how netdesc_t is handled in kernel overall (?)
// TODO: map u80211 errors to errnos
// TODO: map u80211 errors to u80211_drv errors
// TODO: in u80211, change the device ops to take a (void *) handle
// TODO: general netdev cleanup:
// - use struct ops instead of having function pointers in netdev struct
// - proper initialize() function
// - proper destroy() function

static uint64_t id_bitmap;
static int allocate_id(void) {
	for (;;) {
		uint64_t snapshot = __atomic_load_n(&id_bitmap, __ATOMIC_RELAXED);
		if (snapshot == UINT64_MAX)
			return -1;

		int id = __builtin_ctz(~snapshot);
		uint64_t mask = 1lu << id;
		if (__atomic_fetch_or(&id_bitmap, mask, __ATOMIC_RELAXED) & mask)
			continue;

		return id;
	}
}

static void free_id(int id) {
	__atomic_fetch_and(&id_bitmap, ~(1lu << id), __ATOMIC_RELAXED);
}

static int allocate_tx_buffer(u80211_device_t *device, size_t size, u80211_tx_buffer_descriptor_t *buffer_descriptor) {
	wlan_device_t *wlan = device->driver_data;

	buffer_descriptor->size = size;
	buffer_descriptor->current_offset = size;
	return wlan->ops->allocate_tx_buffer(size, &buffer_descriptor->data) == U80211_DRV_STATUS_SUCCESS ?
		U80211_STATUS_SUCCESS : U80211_STATUS_UNKNOWN_ERROR;
}

static int free_tx_buffer(u80211_device_t *device, u80211_tx_buffer_descriptor_t *buffer_descriptor) {
	wlan_device_t *wlan = device->driver_data;
	wlan->ops->free_tx_buffer(buffer_descriptor->data);
	return 0;
}

static int convert_cipher(int cipher, int *drv_cipher) {
	switch (cipher) {
		case -1:
			*drv_cipher = U80211_DRV_CIPHER_NONE;
			return U80211_STATUS_SUCCESS;
		case U80211_CIPHER_CCMP:
			*drv_cipher = U80211_DRV_CIPHER_CCMP;
			return U80211_STATUS_SUCCESS;
		case U80211_CIPHER_TKIP:
			*drv_cipher = U80211_DRV_CIPHER_TKIP;
			return U80211_STATUS_SUCCESS;
		case U80211_CIPHER_WEP40:
			*drv_cipher = U80211_DRV_CIPHER_WEP40;
			return U80211_STATUS_SUCCESS;
		case U80211_CIPHER_WEP104:
			*drv_cipher = U80211_DRV_CIPHER_WEP104;
			return U80211_STATUS_SUCCESS;
		default:
			return U80211_STATUS_UNSUPPORTED;
	}
}

static int transmit(u80211_device_t *device, u80211_tx_buffer_descriptor_t *buffer_descriptor, const u80211_transmit_options_t *options) {
	wlan_device_t *wlan = device->driver_data;

	u80211_drv_transmit_options_t drv_options = {
		.key = options->key
	};
	int status = convert_cipher(options->cipher, &drv_options.cipher);
	if (status != U80211_STATUS_SUCCESS) {
		wlan->ops->free_tx_buffer(buffer_descriptor->data);
		return status;
	}

	return wlan->ops->transmit(wlan->driver_handle, buffer_descriptor->data, buffer_descriptor->size, buffer_descriptor->current_offset, &drv_options) == U80211_DRV_STATUS_SUCCESS ?
		U80211_STATUS_SUCCESS : U80211_STATUS_UNKNOWN_ERROR;
}

static int set_channel(u80211_device_t *device, int channel) {
	wlan_device_t *wlan = device->driver_data;

	return wlan->ops->set_channel(wlan->driver_handle, channel) == U80211_DRV_STATUS_SUCCESS ?
		U80211_STATUS_SUCCESS : U80211_STATUS_UNKNOWN_ERROR;
}

static int set_key(u80211_device_t *device, const u80211_key_t *key) {
	wlan_device_t *wlan = device->driver_data;

	u80211_drv_key_t drv_key = {
		.index = key->index,
		.key = key->key,
		.key_len = key->key_len,
		.flags = key->flags
	};
	memcpy(drv_key.peer, key->peer.bytes, sizeof(drv_key.peer));

	int status = convert_cipher(key->cipher, &drv_key.cipher);
	if (status != U80211_STATUS_SUCCESS)
		return status;

	return wlan->ops->set_key(wlan->driver_handle, &drv_key) == U80211_DRV_STATUS_SUCCESS ?
		U80211_STATUS_SUCCESS : U80211_STATUS_UNKNOWN_ERROR;
}

static int del_key(u80211_device_t *device, uint8_t index, const u80211_mac_address_t *peer, uint32_t flags) {
	(void)peer;
	(void)flags;
	wlan_device_t *wlan = device->driver_data;

	return wlan->ops->del_key(wlan->driver_handle, index) == U80211_DRV_STATUS_SUCCESS ?
		U80211_STATUS_SUCCESS : U80211_STATUS_UNKNOWN_ERROR;
}

static u80211_device_ops_t ops = {
	.allocate_tx_buffer = allocate_tx_buffer,
	.free_tx_buffer = free_tx_buffer,
	.transmit = transmit,
	.set_channel = set_channel,
	.set_key = set_key,
	.del_key = del_key
};

static int wlan_alloc_desc(netdev_t *netdev, size_t requested_size, netdesc_t *desc) {
	wlan_device_t *wlan = (wlan_device_t *)netdev;

	if (requested_size > WLAN_MTU)
		return E2BIG;

	u80211_tx_buffer_descriptor_t u80211_desc;
	int status = u80211_allocate_tx_buffer(wlan->u80211_device, &u80211_desc);
	if (status != U80211_STATUS_SUCCESS)
		return EINVAL;

	desc->address = u80211_desc.data;
	desc->curroffset = u80211_desc.current_offset;
	desc->size = u80211_desc.size;
	return 0;
}

static int wlan_free_desc(netdev_t *netdev, netdesc_t *desc) {
	// TODO: u80211 free descriptor call
	return 0;
}

static int wlan_send_packet(netdev_t *netdev, netdesc_t desc, mac_t target, int proto) {
	wlan_device_t *wlan = (wlan_device_t *)netdev;

	ethframe_t *ethframe = desc.address;
	ethframe->type = cpu_to_be_w(proto);
	memcpy(&ethframe->source, &wlan->netdev.mac, sizeof(mac_t));
	memcpy(&ethframe->destination, &target, sizeof(mac_t));

	u80211_tx_buffer_descriptor_t descriptor = {
		.data = desc.address,
		.current_offset = desc.curroffset,
		.size = desc.size
	};

	return u80211_transmit_buffer(wlan->u80211_device, &descriptor) == U80211_STATUS_SUCCESS ? 0 : EINVAL;
}

int wlan_register(void *handle, const u80211_drv_device_metadata_t *drv_metadata, const u80211_drv_device_ops_t *drv_ops, u80211_drv_network_device_handle_t *network_device) {
	wlan_device_t *wlan = alloc(sizeof(wlan_device_t));
	if (wlan == NULL)
		return ENOMEM;

	wlan->driver_handle = handle;
	wlan->ops = drv_ops;

	wlan->netdev.mtu = WLAN_MTU;
	wlan->netdev.sendpacket = wlan_send_packet;
	wlan->netdev.allocdesc = wlan_alloc_desc;
	wlan->netdev.freedesc = wlan_free_desc;
	wlan->netdev.flags = NETDEV_FLAGS_WLAN;
	memcpy(&wlan->netdev.mac, &drv_metadata->mac_address, 6);
	if (hashtable_init(&wlan->netdev.arpcache, 30)) {
		free(wlan);
		return ENOMEM;
	}

	wlan->id = allocate_id();
	if (wlan->id == -1) {
		hashtable_destroy(&wlan->netdev.arpcache);
		free(wlan);
		return EAGAIN;
	}

	u80211_device_metadata_t metadata;
	memcpy(&metadata.mac_address, &drv_metadata->mac_address, sizeof(metadata.mac_address));
	memcpy(&metadata.rate_bitmap, &drv_metadata->rate_bitmap, sizeof(metadata.rate_bitmap));

	int status = u80211_register_device(&metadata, &ops, wlan, &wlan->u80211_device);
	if (status != U80211_STATUS_SUCCESS) {
		free_id(wlan->id);
		hashtable_destroy(&wlan->netdev.arpcache);
		free(wlan);
		return EINVAL;
	}

	char name[10];
	snprintf(name, 10, "wlan%d", wlan->id);
	int error = netdev_register(&wlan->netdev, name);
	if (error) {
		u80211_unregister_device(wlan->u80211_device);
		free_id(wlan->id);
		hashtable_destroy(&wlan->netdev.arpcache);
		free(wlan);
		return error;
	}

	*network_device = wlan;
	return 0;
}

void wlan_process_packet(u80211_drv_network_device_handle_t handle, void *packet, size_t packet_size) {
	wlan_device_t *wlan = (wlan_device_t *)handle;

	long ipl = interrupt_raiseipl(IPL_DPC);
	u80211_process_packet(wlan->u80211_device, packet, packet_size);
	interrupt_loweripl(ipl);
}

int wlan_active_scan(netdev_t *netdev) {
	wlan_device_t *wlan = (wlan_device_t *)netdev;

	return u80211_scan(wlan->u80211_device) == U80211_STATUS_SUCCESS ? 0 : EINVAL;
}

int wlan_wait_for_scan(netdev_t *netdev) {
	wlan_device_t *wlan = (wlan_device_t *)netdev;

	return u80211_wait_for_scan_completion(wlan->u80211_device) == U80211_STATUS_SUCCESS ? 0 : EINVAL;
}

typedef struct {
	uint16_t length;
	uint16_t rsn_ie_length;
	uint16_t variable_offset;
	uint16_t interval;
	uint16_t capabilities;
	uint8_t bssid[6];
	uint8_t rate_bitmap[16];
	uint8_t channel;
	uint8_t ssid_length;
	// SSID
	// RSN IE
} wlan_bss_info_t;

// this may seem excessive (and, for your average residential air, it is).
// however, some places (like university campi or dense urban environments)
// can easily cross into the hundreds of APs. many different implementations
// have moved to use a 1000 limit to bss entries (see cfg80211 or the discussions on
// hostapd/wpa_supplicant to raise the limit)
#define MAX_APS 1000

int wlan_get_bss_cache(netdev_t *netdev, void *buffer, size_t size, size_t *records_written) {
	wlan_device_t *wlan = (wlan_device_t *)netdev;

	u80211_ap_t **ap_buffer = alloc(sizeof(u80211_ap_t *) * MAX_APS);
	if (ap_buffer == NULL)
		return ENOMEM;

	size_t ap_count = u80211_bss_cache_get_aps(&wlan->u80211_device->bss_cache, ap_buffer, MAX_APS);

	int error = 0;
	*records_written = 0;
	for (size_t i = 0; i < ap_count; ++i) {
		u80211_ap_t *ap = ap_buffer[i];
		wlan_bss_info_t bss_info;
		bss_info.ssid_length = strlen(ap->ssid);
		bss_info.length = sizeof(wlan_bss_info_t) + bss_info.ssid_length + ap->rsn_size;

		if (size < bss_info.length)
			break;

		bss_info.variable_offset = sizeof(bss_info);
		bss_info.rsn_ie_length = ap->rsn_size;
		bss_info.interval = ap->interval;
		bss_info.capabilities = ap->capabilities;
		bss_info.channel = ap->channel;
		memcpy(bss_info.bssid, &ap->mac_address, 6);
		memcpy(bss_info.rate_bitmap, ap->rate_bitmap, sizeof(bss_info.rate_bitmap));

		error = USERCOPY_POSSIBLY_TO_USER(buffer, &bss_info, sizeof(bss_info));
		if (error)
			break;

		if (bss_info.ssid_length) {
			error = USERCOPY_POSSIBLY_TO_USER((void *)((uintptr_t)buffer + sizeof(bss_info)), ap->ssid, bss_info.ssid_length);
			if (error)
				break;
		}

		if (bss_info.rsn_ie_length) {
			error = USERCOPY_POSSIBLY_TO_USER((void *)((uintptr_t)buffer + sizeof(bss_info) + bss_info.ssid_length), ap->rsn, bss_info.rsn_ie_length);
			if (error)
				break;
		}

		size -= bss_info.length;
		buffer = (void *)((uintptr_t)buffer + bss_info.length);
		*records_written += 1;
	}

	for (size_t i = 0; i < ap_count; ++i)
		u80211_ap_release(ap_buffer[i]);

	free(ap_buffer);
	return error;
}

int wlan_associate(netdev_t *netdev, uint8_t bssid[6], void *ie, size_t ie_size) {
	wlan_device_t *wlan = (wlan_device_t *)netdev;

	u80211_mac_address_t mac;
	memcpy(&mac, bssid, 6);

	u80211_ap_t *ap = u80211_bss_cache_find(&wlan->u80211_device->bss_cache, &mac);
	if (ap == NULL)
		return EINVAL;

	int error = u80211_associate(wlan->u80211_device, ap, ie, ie_size) == U80211_STATUS_SUCCESS ? 0 : EINVAL;
	u80211_ap_release(ap);
	return error;
}

int wlan_associate_wait(netdev_t *netdev) {
	wlan_device_t *wlan = (wlan_device_t *)netdev;

	return u80211_wait_for_association_completion(wlan->u80211_device) == U80211_STATUS_SUCCESS ? 0 : EINVAL;
}

int wlan_disassociate(netdev_t *netdev) {
	wlan_device_t *wlan = (wlan_device_t *)netdev;

	return u80211_disassociate(wlan->u80211_device) == U80211_STATUS_SUCCESS ? 0 : EINVAL;
}

static bool wlan_cipher_to_u80211_cipher(uint8_t cipher, u80211_cipher_t *out) {
	switch (cipher) {
		case WLAN_CIPHER_CCMP:
			*out = U80211_CIPHER_CCMP;
			break;
		case WLAN_CIPHER_TKIP:
			*out = U80211_CIPHER_TKIP;
			break;
		case WLAN_CIPHER_WEP40:
			*out = U80211_CIPHER_WEP40;
			break;
		case WLAN_CIPHER_WEP104:
			*out = U80211_CIPHER_WEP104;
			break;
		default:
			return false;
	}

	return true;
}

static uint32_t wlan_flags_to_u80211_flags(uint32_t flags) {
	uint32_t out = 0;

	if (flags & WLAN_KEY_PAIRWISE)
		out |= U80211_KEY_PAIRWISE;

	if (flags & WLAN_KEY_GROUP)
		out |= U80211_KEY_GROUP;

	if (flags & WLAN_KEY_RX)
		out |= U80211_KEY_RX;

	if (flags & WLAN_KEY_TX)
		out |= U80211_KEY_TX;

	return out;
}

int wlan_set_key(netdev_t *netdev, wlan_key_t *key) {
	wlan_device_t *wlan = (wlan_device_t *)netdev;

	u80211_key_t u80211_key = {
		.index = key->index,
		.key = key->key,
		.key_len = key->key_len,
		.rx_seq = key->rx_seq,
		.rx_seq_len = key->rx_seq_len,
		.flags = wlan_flags_to_u80211_flags(key->flags)
	};
	memcpy(&u80211_key.peer, key->peer, 6);

	if (!wlan_cipher_to_u80211_cipher(key->cipher, &u80211_key.cipher))
		return EINVAL;

	return u80211_set_key(wlan->u80211_device, &u80211_key) == U80211_STATUS_SUCCESS ? 0 : EINVAL;
}

int wlan_del_key(netdev_t *netdev, uint8_t index, uint8_t peer[6], uint32_t flags) {
	wlan_device_t *wlan = (wlan_device_t *)netdev;

	u80211_mac_address_t mac;
	memcpy(&mac, peer, 6);

	return u80211_del_key(wlan->u80211_device, index, &mac, flags) == U80211_STATUS_SUCCESS ? 0 : EINVAL;
}
