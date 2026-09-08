#ifndef U80211_SCAN_H
#define U80211_SCAN_H

#include <u80211/packet.h>

void u80211_scan_process_response(u80211_device_t *device, u80211_beacon_data_t *beacon_data);

#endif
