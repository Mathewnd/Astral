#ifndef U80211_DRV_80211_H
#define U80211_DRV_80211_H

#include <stdint.h>

#include <u80211_drv/util.h>

#define U80211_DRV_80211_FRAME_TYPE_MANAGEMENT 0
#define U80211_DRV_80211_FRAME_TYPE_DATA 2

#define U80211_DRV_80211_FRAME_DATA_QOS_BIT 0x8
#define U80211_DRV_80211_HEADER_MINIMUM_SIZE 24
#define U80211_DRV_80211_MAX_MPDU_SIZE 2352

static inline uint16_t u80211_drv_80211_frame_control(const void *frame) {
	return u80211_drv_deserialize_le16(frame);
}

static inline uint8_t u80211_drv_80211_frame_type(uint16_t frame_control) {
	return (frame_control >> 2) & 0x3;
}

static inline uint8_t u80211_drv_80211_frame_subtype(uint16_t frame_control) {
	return (frame_control >> 4) & 0xf;
}

static inline const uint8_t *u80211_drv_80211_receiver_address(const void *frame) {
	const uint8_t *frame_bytes = frame;
	return frame_bytes + 4;
}

static inline uint16_t u80211_drv_80211_sequence_number(const void *frame) {
	const uint8_t *frame_bytes = frame;
	return u80211_drv_deserialize_le16(frame_bytes + 22) >> 4;
}

#endif
