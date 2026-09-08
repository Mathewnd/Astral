#ifndef U80211_AP_H
#define U80211_AP_H

#include <u80211/packet.h>
#include <u80211/rbtree.h>

typedef struct {
	int refcount;
	u80211_mac_address_t mac_address;
	uint16_t interval;
	uint16_t capabilities;
	uint8_t channel;
	uint8_t rate_bitmap[16];
	char ssid[33];
	uint8_t *rsn;
	size_t rsn_size;
	u80211_rbtree_t cache_node;
} u80211_ap_t;

u80211_ap_t *u80211_ap_allocate(const u80211_beacon_data_t *beacon_data);
void u80211_ap_inactive(u80211_ap_t *ap);

static inline void u80211_ap_hold(u80211_ap_t *ap) {
	__atomic_add_fetch(&ap->refcount, 1, __ATOMIC_RELAXED);
}

static inline void u80211_ap_release(u80211_ap_t *ap) {
	if (__atomic_sub_fetch(&ap->refcount, 1, __ATOMIC_RELAXED) == 0)
		u80211_ap_inactive(ap);
}

#endif
