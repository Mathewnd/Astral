#ifndef U80211_RINGBUFFER_H
#define U80211_RINGBUFFER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	size_t size;
	uintmax_t write;
	uintmax_t read;
	void *data;
} u80211_ringbuffer_t;

int u80211_ringbuffer_init(u80211_ringbuffer_t *ringbuffer, size_t size);
void u80211_ringbuffer_destroy(u80211_ringbuffer_t *ringbuffer);

size_t u80211_ringbuffer_read(u80211_ringbuffer_t *ringbuffer, void *buffer, size_t count);
size_t u80211_ringbuffer_write(u80211_ringbuffer_t *ringbuffer, const void *buffer, size_t count);
size_t u80211_ringbuffer_truncate(u80211_ringbuffer_t *ringbuffer, size_t count);
size_t u80211_ringbuffer_peek(const u80211_ringbuffer_t *ringbuffer, void *buffer, uintmax_t offset, size_t count);
size_t u80211_ringbuffer_remove(u80211_ringbuffer_t *ringbuffer, size_t count);

#define U80211_RINGBUFFER_DATA_COUNT(x) ((x)->write - (x)->read)
#define U80211_RINGBUFFER_SIZE(x) ((x)->size)
#define U80211_RINGBUFFER_FREE_SPACE(x) (U80211_RINGBUFFER_SIZE(x) - U80211_RINGBUFFER_DATA_COUNT(x))

#endif
