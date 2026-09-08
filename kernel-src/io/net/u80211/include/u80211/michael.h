#ifndef U80211_MICHAEL_H
#define U80211_MICHAEL_H

#include <stddef.h>
#include <stdint.h>

void u80211_michael_mic(const uint8_t key[8], const uint8_t da[6], const uint8_t sa[6], uint8_t priority, const void *data, size_t len, uint8_t out[8]);

#endif
