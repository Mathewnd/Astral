#ifndef U80211_BSS_CACHE_H
#define U80211_BSS_CACHE_H

#include <stddef.h>

#include <u80211/ap.h>

typedef struct {
	void *rwlock;
	u80211_rbtree_t *root;
	size_t entry_count;
} bss_cache_t;

int u80211_bss_cache_init(bss_cache_t *cache);
void u80211_bss_cache_deinit(bss_cache_t *cache);
void u80211_bss_cache_purge(bss_cache_t *cache);
void u80211_bss_cache_insert(bss_cache_t *cache, u80211_ap_t *ap);
void u80211_bss_cache_remove(bss_cache_t *cache, u80211_mac_address_t *mac);
u80211_ap_t *u80211_bss_cache_find(bss_cache_t *cache, u80211_mac_address_t *mac);
// each AP pointer written to buffer has an acquired reference that the caller must release.
size_t u80211_bss_cache_get_aps(bss_cache_t *cache, u80211_ap_t **buffer, size_t capacity);
size_t u80211_bss_cache_get_count(bss_cache_t *cache);

#endif
