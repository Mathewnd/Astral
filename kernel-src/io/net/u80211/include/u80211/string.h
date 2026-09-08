#ifndef U80211_STRING_H
#define U80211_STRING_H

#include <stddef.h>

void *u80211_memcpy(void *destination, const void *source, size_t size);
int u80211_memcmp(const void *a, const void *b, size_t size);
void *u80211_memset(void *destination, int value, size_t size);

#endif
