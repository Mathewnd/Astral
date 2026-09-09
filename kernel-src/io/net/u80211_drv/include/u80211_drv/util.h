#ifndef U80211_DRV_UTIL_H
#define U80211_DRV_UTIL_H

#include <stdint.h>

#include <u80211_drv/string.h>

static inline uint16_t u80211_drv_host_to_le16(uint16_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return value;
#else
	return __builtin_bswap16(value);
#endif
}

static inline uint32_t u80211_drv_host_to_le32(uint32_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return value;
#else
	return __builtin_bswap32(value);
#endif
}

static inline uint16_t u80211_drv_le_to_host16(uint16_t value) {
	return u80211_drv_host_to_le16(value);
}

static inline uint32_t u80211_drv_le_to_host32(uint32_t value) {
	return u80211_drv_host_to_le32(value);
}

static inline uint16_t u80211_drv_deserialize_le16(const void *source) {
	uint16_t value;
	u80211_drv_memcpy(&value, source, sizeof(value));
	return u80211_drv_le_to_host16(value);
}

static inline uint32_t u80211_drv_deserialize_le32(const void *source) {
	uint32_t value;
	u80211_drv_memcpy(&value, source, sizeof(value));
	return u80211_drv_le_to_host32(value);
}

static inline void u80211_drv_serialize_le16(uint8_t *destination, uint16_t value) {
	uint16_t little_endian_value = u80211_drv_host_to_le16(value);
	u80211_drv_memcpy(destination, &little_endian_value, sizeof(little_endian_value));
}

static inline void u80211_drv_serialize_le32(uint8_t *destination, uint32_t value) {
	uint32_t little_endian_value = u80211_drv_host_to_le32(value);
	u80211_drv_memcpy(destination, &little_endian_value, sizeof(little_endian_value));
}

#endif
