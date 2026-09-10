#include <kernel/wlan.h>
#include <u80211/u80211.h>
#include <kernel/net.h>
#include <kernel/eth.h>
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

// TODO: change how netdesc_t is handled in kernel overall (?)
// TODO: map u80211 errors to errnos
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
}

static int free_tx_buffer(u80211_device_t *device, u80211_tx_buffer_descriptor_t *buffer_descriptor) {
	wlan_device_t *wlan = device->driver_data;
}

static int transmit(u80211_device_t *device, u80211_tx_buffer_descriptor_t *buffer_descriptor, const u80211_transmit_options_t *options) {
	wlan_device_t *wlan = device->driver_data;

}

static int set_channel(u80211_device_t *device, int channel) {
	wlan_device_t *wlan = device->driver_data;

}

static int set_key(u80211_device_t *device, const u80211_key_t *key) {
	wlan_device_t *wlan = device->driver_data;

}

static int del_key(u80211_device_t *device, uint8_t index, const u80211_mac_address_t *peer, uint32_t flags) {
	wlan_device_t *wlan = device->driver_data;

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
	}
	return error;
}
