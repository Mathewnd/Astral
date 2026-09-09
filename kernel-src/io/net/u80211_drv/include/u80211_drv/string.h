#ifndef U80211_DRV_STRING_H
#define U80211_DRV_STRING_H

#include <stddef.h>

void *u80211_drv_memcpy(void *destination, const void *source, size_t size);
void *u80211_drv_memset(void *destination, int value, size_t size);

#endif
