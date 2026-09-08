#include <stddef.h>

#include <u80211/bss_cache.h>
#include <u80211/kernel_interface.h>
#include <u80211/rbtree.h>
#include <u80211/status.h>
#include <u80211/util.h>

static int mac_compare(u80211_mac_address_t *a, u80211_mac_address_t *b) {
	for (size_t i = 0; i < sizeof(a->bytes); i++) {
		if (a->bytes[i] < b->bytes[i])
			return -1;
		if (a->bytes[i] > b->bytes[i])
			return 1;
	}

	return 0;
}

static int rbtree_mac_compare(u80211_rbtree_t *a, u80211_rbtree_t *b) {
	u80211_ap_t *ap_a = container_of(a, u80211_ap_t, cache_node);
	u80211_ap_t *ap_b = container_of(b, u80211_ap_t, cache_node);

	return mac_compare(&ap_a->mac_address, &ap_b->mac_address);
}

static int rbtree_value_compare(void *a, u80211_rbtree_t *b) {
	u80211_ap_t *ap_b = container_of(b, u80211_ap_t, cache_node);

	return mac_compare(a, &ap_b->mac_address);
}

int u80211_bss_cache_init(bss_cache_t *cache) {
	cache->root = NULL;
	cache->entry_count = 0;
	cache->rwlock = u80211_kernel_allocate_rwlock();
	if (unlikely(cache->rwlock == NULL))
		return U80211_STATUS_ENOMEM;

	return U80211_STATUS_SUCCESS;
}

void u80211_bss_cache_deinit(bss_cache_t *cache) {
	u80211_kernel_acquire_rwlock_exclusive(cache->rwlock);

	u80211_rbtree_t *iterator = cache->root == NULL ? NULL : u80211_rbtree_first(cache->root);
	while (iterator != NULL) {
		u80211_rbtree_t *next = u80211_rbtree_successor(iterator);
		u80211_ap_t *ap = container_of(iterator, u80211_ap_t, cache_node);

		u80211_rbtree_remove(&cache->root, iterator);
		--cache->entry_count;
		u80211_ap_release(ap);
		iterator = next;
	}

	u80211_kernel_release_rwlock_exclusive(cache->rwlock);
	u80211_kernel_free_rwlock(cache->rwlock);
	cache->rwlock = NULL;
	cache->root = NULL;
}

void u80211_bss_cache_purge(bss_cache_t *cache) {
	u80211_kernel_acquire_rwlock_exclusive(cache->rwlock);

	u80211_rbtree_t *iterator = cache->root == NULL ? NULL : u80211_rbtree_first(cache->root);
	while (iterator != NULL) {
		u80211_rbtree_t *next = u80211_rbtree_successor(iterator);

		u80211_ap_t *ap = container_of(iterator, u80211_ap_t, cache_node);
		if (__atomic_load_n(&ap->refcount, __ATOMIC_RELAXED) == 1) {
			u80211_rbtree_remove(&cache->root, iterator);
			--cache->entry_count;
			u80211_ap_release(ap);
		}

		iterator = next;
	}

	u80211_kernel_release_rwlock_exclusive(cache->rwlock);
}

void u80211_bss_cache_insert(bss_cache_t *cache, u80211_ap_t *ap) {
	u80211_kernel_acquire_rwlock_exclusive(cache->rwlock);

	u80211_rbtree_t *node = u80211_rbtree_lookup(cache->root, &ap->mac_address, rbtree_value_compare);
	if (node != NULL) {
		u80211_kernel_release_rwlock_exclusive(cache->rwlock);
		return;
	}

	u80211_rbtree_insert(&cache->root, &ap->cache_node, rbtree_mac_compare);
	u80211_ap_hold(ap);
	++cache->entry_count;

	u80211_kernel_release_rwlock_exclusive(cache->rwlock);
}

void u80211_bss_cache_remove(bss_cache_t *cache, u80211_mac_address_t *mac) {
	u80211_kernel_acquire_rwlock_exclusive(cache->rwlock);

	u80211_rbtree_t *node = u80211_rbtree_lookup(cache->root, mac, rbtree_value_compare);
	if (node == NULL) {
		u80211_kernel_release_rwlock_exclusive(cache->rwlock);
		return;
	}

	u80211_rbtree_remove(&cache->root, node);
	--cache->entry_count;

	u80211_ap_t *ap = container_of(node, u80211_ap_t, cache_node);
	u80211_ap_release(ap);

	u80211_kernel_release_rwlock_exclusive(cache->rwlock);
}

u80211_ap_t *u80211_bss_cache_find(bss_cache_t *cache, u80211_mac_address_t *mac) {
	u80211_kernel_acquire_rwlock_shared(cache->rwlock);

	u80211_rbtree_t *node = u80211_rbtree_lookup(cache->root, mac, rbtree_value_compare);
	if (node == NULL) {
		u80211_kernel_release_rwlock_shared(cache->rwlock);
		return NULL;
	}

	u80211_ap_t *ap = container_of(node, u80211_ap_t, cache_node);
	u80211_ap_hold(ap);

	u80211_kernel_release_rwlock_shared(cache->rwlock);

	return ap;
}

size_t u80211_bss_cache_get_aps(bss_cache_t *cache, u80211_ap_t **buffer, size_t capacity) {
	u80211_kernel_acquire_rwlock_shared(cache->rwlock);

	size_t count = 0;
	u80211_rbtree_t *iterator = cache->root == NULL ? NULL : u80211_rbtree_first(cache->root);
	while (iterator != NULL && count < capacity) {
		u80211_ap_t *ap = container_of(iterator, u80211_ap_t, cache_node);
		u80211_ap_hold(ap);
		buffer[count++] = ap;
		iterator = u80211_rbtree_successor(iterator);
	}

	u80211_kernel_release_rwlock_shared(cache->rwlock);

	return count;
}

size_t u80211_bss_cache_get_count(bss_cache_t *cache) {
	u80211_kernel_acquire_rwlock_shared(cache->rwlock);
	size_t count = cache->entry_count;
	u80211_kernel_release_rwlock_shared(cache->rwlock);

	return count;
}
