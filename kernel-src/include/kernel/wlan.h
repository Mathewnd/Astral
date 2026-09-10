#ifndef _WLAN_H
#define _WLAN_H

#include <u80211_drv/kernel_interface.h>
#include <kernel/net.h>

#define WLAN_CIPHER_CCMP 0
#define WLAN_CIPHER_TKIP 1
#define WLAN_CIPHER_WEP40 2
#define WLAN_CIPHER_WEP104 3

#define WLAN_KEY_PAIRWISE 1
#define WLAN_KEY_GROUP 2
#define WLAN_KEY_RX 4
#define WLAN_KEY_TX 8

typedef struct {
	uint8_t cipher;
	uint8_t index;
	uint8_t peer[6];
	uint8_t *key;
	size_t key_len;
	uint8_t *rx_seq;
	size_t rx_seq_len;
	uint32_t flags;
} wlan_key_t;

int wlan_register(void *handle, const u80211_drv_device_metadata_t *metadata, const u80211_drv_device_ops_t *ops, u80211_drv_network_device_handle_t *network_device);
void wlan_process_packet(u80211_drv_network_device_handle_t handle, void *packet, size_t packet_size);

int wlan_active_scan(netdev_t *netdev);
int wlan_wait_for_scan(netdev_t *netdev);

int wlan_get_bss_cache(netdev_t *netdev, void *buffer, size_t size, size_t *records_written);

int wlan_associate(netdev_t *netdev, uint8_t bssid[6], void *ie, size_t ie_size);
int wlan_associate_wait(netdev_t *netdev);
int wlan_disassociate(netdev_t *netdev);

int wlan_set_key(netdev_t *netdev, wlan_key_t *key);
int wlan_del_key(netdev_t *netdev, uint8_t index, uint8_t peer[6], uint32_t flags);

#endif
