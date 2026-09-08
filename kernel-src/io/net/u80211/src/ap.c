#include <u80211/ap.h>
#include <u80211/kernel_interface.h>
#include <u80211/string.h>

u80211_ap_t *u80211_ap_allocate(const u80211_beacon_data_t *beacon_data) {
	u80211_ap_t *ap = u80211_kernel_allocate(sizeof(*ap));
	if (ap == NULL)
		return NULL;

	ap->rsn_size = beacon_data->rsn_size;
	if (ap->rsn_size) {
		ap->rsn = u80211_kernel_allocate(ap->rsn_size);
		if (ap->rsn == NULL) {
			u80211_kernel_free(ap);
			return NULL;
		}

		u80211_memcpy(ap->rsn, beacon_data->rsn, ap->rsn_size);
	} else {
		ap->rsn = NULL;
	}

	__atomic_store_n(&ap->refcount, 1, __ATOMIC_RELAXED);
	ap->mac_address = beacon_data->mac_address;
	ap->interval = beacon_data->interval;
	ap->capabilities = beacon_data->capabilities;
	ap->channel = beacon_data->channel;
	u80211_memcpy(ap->rate_bitmap, beacon_data->rate_bitmap, sizeof(ap->rate_bitmap));
	u80211_memcpy(ap->ssid, beacon_data->ssid, sizeof(ap->ssid));
	return ap;
}

void u80211_ap_inactive(u80211_ap_t *ap) {
	__atomic_thread_fence(__ATOMIC_ACQ_REL);

	if (ap->rsn != NULL)
		u80211_kernel_free(ap->rsn);

	u80211_kernel_free(ap);
}
