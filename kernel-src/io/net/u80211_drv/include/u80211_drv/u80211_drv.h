#ifndef U80211_DRV_U80211_DRV_H
#define U80211_DRV_U80211_DRV_H

#include <stdint.h>

#include <u80211_drv/kernel_interface.h>
#include <u80211_drv/status.h>

#ifdef __cplusplus
extern "C" {
#endif

// probes a interface to check if there is a matching driver for a specific usb device
// return U80211_DRV_STATUS_SUCCESS when there is a match
int u80211_drv_probe(u80211_drv_device_handle_t device, u80211_drv_interface_handle_t interface);

// attaches an interface to a driver
// returns U80211_DRV_STATUS_SUCCESS when driver successfully attaches the interface
int u80211_drv_attach(u80211_drv_device_handle_t device, u80211_drv_interface_handle_t interface);

#ifdef __cplusplus
}
#endif

#endif
