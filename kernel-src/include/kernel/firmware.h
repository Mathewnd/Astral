#ifndef _FIRMWARE_H
#define _FIRMWARE_H

#include <stddef.h>

typedef void (*firmware_callback_t)(void *ctx, void *data, size_t size);

// firmware data is only valid only for the duration of the callback
int firmware_load(const char *name, firmware_callback_t callback, void *ctx);

#endif
