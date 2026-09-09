#include <u80211/u80211.h>
#include <u80211/packet.h>
#include <u80211/kernel_interface.h>
#include <u80211/bss_cache.h>
#include <u80211/regulatory_database.h>
#include <u80211/ringbuffer.h>
#include <u80211/scan.h>
#include <u80211/status.h>
#include <u80211/string.h>
#include <u80211/util.h>
#include <stdbool.h>

#define WAIT_MS 75
#define BUFFER_COUNT 64
#define MAX_RSN_SIZE UINT8_MAX
#define BUFFER_SIZE (BUFFER_COUNT * (sizeof(u80211_beacon_data_t) + MAX_RSN_SIZE))

typedef struct {
	u80211_ringbuffer_t buffer;
	uint8_t rsn[MAX_RSN_SIZE]; // use this as a small scratch space
	u80211_device_t *device;
	bool receiving;
	int current_channel;
	void *work;
	void *timer;
} u80211_scan_state_t;

typedef struct {
	u80211_list_node_t node;
	void *semaphore;
} u80211_scan_waiter_t;

static void wake_scan_waiters(u80211_device_t *device) {
	u80211_list_node_t *node;
	while ((node = u80211_list_pop_front(&device->scan_waiters)) != NULL) {
		u80211_scan_waiter_t *waiter = container_of(node, u80211_scan_waiter_t, node);
		u80211_kernel_signal_semaphore(waiter->semaphore);
	}
}

static void destroy_scan_state(u80211_scan_state_t *scan_state) {
	u80211_kernel_free_timer(scan_state->timer);
	u80211_kernel_free_work(scan_state->work);
	u80211_ringbuffer_destroy(&scan_state->buffer);
	u80211_kernel_free(scan_state);
}

static void cleanup_scan(void *ctx);

static void complete_scan(u80211_scan_state_t *scan_state) {
	u80211_device_t *device = scan_state->device;

	u80211_kernel_acquire_spinlock(device->scan_spinlock);
	device->scan_context = NULL;
	u80211_kernel_release_spinlock(device->scan_spinlock);

	u80211_kernel_enqueue_work(device->scan_cleanup_work, cleanup_scan, scan_state);
}

static void cleanup_scan(void *ctx) {
	u80211_scan_state_t *scan_state = ctx;
	u80211_device_t *device = scan_state->device;

	destroy_scan_state(scan_state);

	u80211_kernel_acquire_spinlock(device->scan_spinlock);
	u80211_set_device_state(device, U80211_DEVICE_STATE_SCANNING, U80211_DEVICE_STATE_DOWN);
	wake_scan_waiters(device);
	u80211_kernel_release_spinlock(device->scan_spinlock);
}

void u80211_scan_process_response(u80211_device_t *device, u80211_beacon_data_t *beacon_data) {
	u80211_kernel_acquire_spinlock(device->scan_spinlock);
	u80211_scan_state_t *scan_state = device->scan_context;

	if (scan_state == NULL || !scan_state->receiving || beacon_data->rsn_size > MAX_RSN_SIZE) {
		u80211_kernel_release_spinlock(device->scan_spinlock);
		return;
	}

	size_t record_size = sizeof(*beacon_data) + beacon_data->rsn_size;
	if (U80211_RINGBUFFER_FREE_SPACE(&scan_state->buffer) < record_size) {
		u80211_kernel_release_spinlock(device->scan_spinlock);
		return;
	}

	if (beacon_data->channel == 0)
		beacon_data->channel = scan_state->current_channel;

	u80211_ringbuffer_write(&scan_state->buffer, beacon_data, sizeof(*beacon_data));
	u80211_ringbuffer_write(&scan_state->buffer, beacon_data->rsn, beacon_data->rsn_size);

	u80211_kernel_release_spinlock(device->scan_spinlock);
}

static bool valid_bssid(const u80211_mac_address_t *mac_address) {
	bool all_zero = true;
	bool all_broadcast = true;
	for (size_t i = 0; i < sizeof(mac_address->bytes); ++i) {
		if (mac_address->bytes[i] != 0)
			all_zero = false;
		if (mac_address->bytes[i] != 0xff)
			all_broadcast = false;
	}

	return !all_zero && !all_broadcast && !(mac_address->bytes[0] & 1);
}

// TODO: this obviously won't work for anything other than 2.4ghz wifi
static int next_channel(int current) {
	for (int channel = current + 1; channel <= 14; ++channel) {
		if (u80211_get_channel_rules(channel).flags & (U80211_CHANNEL_RULES_DISABLED | U80211_CHANNEL_RULES_PASSIVE))
			continue;

		return channel;
	}

	return -1;
}

static void scan_work(void *ctx) {
	u80211_scan_state_t *scan_state = ctx;
	u80211_device_t *device = scan_state->device;

	u80211_kernel_acquire_spinlock(device->scan_spinlock);
	scan_state->receiving = false;
	u80211_kernel_release_spinlock(device->scan_spinlock);

	while (U80211_RINGBUFFER_DATA_COUNT(&scan_state->buffer) >= sizeof(u80211_beacon_data_t)) {
		u80211_beacon_data_t beacon_data;
		if (u80211_ringbuffer_read(&scan_state->buffer, &beacon_data, sizeof(beacon_data)) != sizeof(beacon_data))
			break;

		if (u80211_ringbuffer_read(&scan_state->buffer, scan_state->rsn, beacon_data.rsn_size) != beacon_data.rsn_size)
			break;
		beacon_data.rsn = beacon_data.rsn_size == 0 ? NULL : scan_state->rsn;

		if (beacon_data.channel != scan_state->current_channel || !valid_bssid(&beacon_data.mac_address))
			continue;

		u80211_ap_t *ap = u80211_ap_allocate(&beacon_data);
		if (ap == NULL)
			continue;

		u80211_bss_cache_insert(&device->bss_cache, ap);
		u80211_ap_release(ap);
	}

	int current_channel = next_channel(scan_state->current_channel);
	if (current_channel < 0) {
		complete_scan(scan_state);
		return;
	}

	u80211_kernel_acquire_spinlock(device->scan_spinlock);
	scan_state->current_channel = current_channel;
	u80211_kernel_release_spinlock(device->scan_spinlock);

	device->ops->set_channel(device, scan_state->current_channel);

	u80211_kernel_acquire_spinlock(device->scan_spinlock);
	scan_state->receiving = true;
	u80211_kernel_release_spinlock(device->scan_spinlock);

	u80211_send_probe_request(device);

	u80211_kernel_enqueue_delayed_work(scan_state->work, scan_state->timer, scan_work, scan_state, WAIT_MS);
}

int u80211_scan(u80211_device_t *device) {
	u80211_scan_state_t *scan_state = u80211_kernel_allocate(sizeof(u80211_scan_state_t));
	if (scan_state == NULL)
		return U80211_STATUS_ENOMEM;

	int status = u80211_ringbuffer_init(&scan_state->buffer, BUFFER_SIZE);
	if (status != U80211_STATUS_SUCCESS) {
		u80211_kernel_free(scan_state);
		return status;
	}

	scan_state->work = u80211_kernel_allocate_work();
	if (scan_state->work == NULL) {
		u80211_ringbuffer_destroy(&scan_state->buffer);
		u80211_kernel_free(scan_state);
		return U80211_STATUS_ENOMEM;
	}

	scan_state->timer = u80211_kernel_allocate_timer();
	if (scan_state->timer == NULL) {
		u80211_kernel_free_work(scan_state->work);
		u80211_ringbuffer_destroy(&scan_state->buffer);
		u80211_kernel_free(scan_state);
		return U80211_STATUS_ENOMEM;
	}

	scan_state->receiving = false;
	scan_state->device = device;
	scan_state->current_channel = next_channel(0);
	if (scan_state->current_channel < 0) {
		destroy_scan_state(scan_state);
		return U80211_STATUS_NOT_PERMITTED;
	}

	if (!u80211_set_device_state(device, U80211_DEVICE_STATE_DOWN, U80211_DEVICE_STATE_SCANNING)) {
		destroy_scan_state(scan_state);
		return U80211_STATUS_BUSY;
	}

	u80211_kernel_acquire_spinlock(device->scan_spinlock);
	device->scan_context = scan_state;
	u80211_kernel_release_spinlock(device->scan_spinlock);

	u80211_bss_cache_purge(&device->bss_cache);

	device->ops->set_channel(device, scan_state->current_channel);

	u80211_kernel_acquire_spinlock(device->scan_spinlock);
	scan_state->receiving = true;
	u80211_kernel_release_spinlock(device->scan_spinlock);

	u80211_send_probe_request(device);

	u80211_kernel_enqueue_delayed_work(scan_state->work, scan_state->timer, scan_work, scan_state, WAIT_MS);

	return U80211_STATUS_SUCCESS;
}

int u80211_wait_for_scan_completion(u80211_device_t *device) {
	void *semaphore = u80211_kernel_allocate_semaphore(0);
	if (semaphore == NULL)
		return U80211_STATUS_RETRY;

	u80211_scan_waiter_t waiter = {
		.semaphore = semaphore,
	};

	u80211_kernel_acquire_spinlock(device->scan_spinlock);
	if (u80211_get_device_state(device) != U80211_DEVICE_STATE_SCANNING) {
		u80211_kernel_release_spinlock(device->scan_spinlock);
		u80211_kernel_free_semaphore(semaphore);
		return U80211_STATUS_SUCCESS;
	}

	u80211_list_push_back(&device->scan_waiters, &waiter.node);
	u80211_kernel_release_spinlock(device->scan_spinlock);

	u80211_kernel_wait_semaphore(semaphore);
	u80211_kernel_free_semaphore(semaphore);
	return U80211_STATUS_SUCCESS;
}
