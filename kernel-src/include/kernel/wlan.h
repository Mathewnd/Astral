#ifndef _WLAN_H
#define _WLAN_H

#include <u80211_drv/kernel_interface.h>

int wlan_register(void *handle, const u80211_drv_device_metadata_t *metadata, const u80211_drv_device_ops_t *ops, u80211_drv_network_device_handle_t *network_device);
void wlan_process_packet(u80211_drv_network_device_handle_t handle, void *packet, size_t packet_size);

#endif
