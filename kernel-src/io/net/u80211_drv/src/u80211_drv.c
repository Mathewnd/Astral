#include <u80211_drv/u80211_drv.h>
#include <u80211_drv/status.h>
#include <u80211_drv/kernel_interface.h>
#include <u80211_drv/drv_init.h>
#include <stddef.h>
#include <stdint.h>

typedef int (*u80211_drv_initialize_fn_t)(u80211_drv_device_handle_t device, u80211_drv_interface_handle_t interface);

typedef struct {
	uint32_t id;
	uint32_t interface;
	u80211_drv_initialize_fn_t fn;
	const char *name;
} u80211_drv_match_table_entry_t;

static const u80211_drv_match_table_entry_t match_table[] = {
	{0x20013310, 0xffffff, u80211_drv_rtl8188eu_init, "DWA-123 (Revision D1, rtl8188eu driver)"},
};

#define MATCH_TABLE_ENTRIES (sizeof(match_table) / sizeof(*match_table))

static const u80211_drv_match_table_entry_t *get_table_entry(const u80211_drv_device_descriptor_t *device_descriptor, 
		const u80211_drv_interface_descriptor_t *interface_descriptor) {
	uint32_t id = ((uint32_t)device_descriptor->vendor_id << 16) | device_descriptor->product_id;
	uint32_t interface = ((uint32_t)interface_descriptor->class_code << 16) | ((uint32_t)interface_descriptor->subclass << 8) | interface_descriptor->protocol;

	for (size_t i = 0; i < MATCH_TABLE_ENTRIES; ++i) {
		if (match_table[i].id == id && match_table[i].interface == interface)
			return &match_table[i];
	}

	return NULL;
}

int u80211_drv_probe(u80211_drv_device_handle_t device, u80211_drv_interface_handle_t interface) {
	u80211_drv_device_descriptor_t device_descriptor;
	u80211_drv_interface_descriptor_t interface_descriptor;
	if (u80211_drv_kernel_get_device_descriptor(device, &device_descriptor) != U80211_DRV_STATUS_SUCCESS || 
			u80211_drv_kernel_get_interface_descriptor(interface, &interface_descriptor) != U80211_DRV_STATUS_SUCCESS)
		return U80211_DRV_STATUS_UNKNOWN_ERROR;

	const u80211_drv_match_table_entry_t *entry = get_table_entry(&device_descriptor, &interface_descriptor);

	return entry ? U80211_DRV_STATUS_SUCCESS : U80211_DRV_STATUS_NO_MATCH;
}

int u80211_drv_attach(u80211_drv_device_handle_t device, u80211_drv_interface_handle_t interface) {
	u80211_drv_device_descriptor_t device_descriptor;
	u80211_drv_interface_descriptor_t interface_descriptor;
	if (u80211_drv_kernel_get_device_descriptor(device, &device_descriptor) != U80211_DRV_STATUS_SUCCESS || 
			u80211_drv_kernel_get_interface_descriptor(interface, &interface_descriptor) != U80211_DRV_STATUS_SUCCESS)
		return U80211_DRV_STATUS_UNKNOWN_ERROR;

	const u80211_drv_match_table_entry_t *entry = get_table_entry(&device_descriptor, &interface_descriptor);

	if (entry == NULL)
		return U80211_DRV_STATUS_NO_MATCH;

	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, "Matched device:");
	u80211_drv_kernel_print(U80211_DRV_KERNEL_PRINT_LEVEL_INFO, entry->name);

	return entry->fn(device, interface);
}
