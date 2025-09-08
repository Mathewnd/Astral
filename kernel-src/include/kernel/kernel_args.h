#ifndef _KERNEL_ARGS_H
#define _KERNEL_ARGS_H

#include <stdbool.h>
#include <stdint.h>

void kernel_arguments_parse(void);

#define KERNEL_ARGUMENT_TYPE_BOOL 0
#define KERNEL_ARGUMENT_TYPE_STR 1
#define KERNEL_ARGUMENT_TYPE_INT 2

typedef struct {
	const char *key;
	int type;
	bool found;
	union {
		char *value_str;
		uint64_t value_int;
	};
} kernel_argument_t;

#define DEFINE_KERNEL_ARGUMENT(name, t) \
	static kernel_argument_t kernel_argument_##name = { \
		.key = #name , \
		.type = _Generic((t)0, \
			uint8_t: KERNEL_ARGUMENT_TYPE_INT, \
			uint16_t: KERNEL_ARGUMENT_TYPE_INT, \
			uint32_t: KERNEL_ARGUMENT_TYPE_INT, \
			uint64_t: KERNEL_ARGUMENT_TYPE_INT, \
			int8_t: KERNEL_ARGUMENT_TYPE_INT, \
			int16_t: KERNEL_ARGUMENT_TYPE_INT, \
			int32_t: KERNEL_ARGUMENT_TYPE_INT, \
			int64_t: KERNEL_ARGUMENT_TYPE_INT, \
			bool: KERNEL_ARGUMENT_TYPE_BOOL, \
			char *: KERNEL_ARGUMENT_TYPE_STR, \
			default: (kernel_argument_t *)NULL \
		), \
		.found = false \
	}; \
	__attribute__((section(".kernel_arguments"), used)) static kernel_argument_t *kernel_argument_ptr_##name = &kernel_argument_##name;

#define GET_KERNEL_ARGUMENT(name, t) _Generic((t)0, \
			uint8_t: kernel_argument_##name.value_int, \
			uint16_t: kernel_argument_##name.value_int, \
			uint32_t: kernel_argument_##name.value_int, \
			uint64_t: kernel_argument_##name.value_int, \
			int8_t: kernel_argument_##name.value_int, \
			int16_t: kernel_argument_##name.value_int, \
			int32_t: kernel_argument_##name.value_int, \
			int64_t: kernel_argument_##name.value_int, \
			bool: kernel_argument_##name.found, \
			char *: kernel_argument_##name.value_str, \
			default: (kernel_argument_t *)NULL \
		)

#endif
