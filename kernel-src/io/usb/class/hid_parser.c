#include <kernel/usb.h>
#include <kernel/usb_hid.h>
#include <logging.h>
#include <kernel/alloc.h>

#define HID_ITEM_LONG_PREFIX 0xfe

#define HID_ITEM_TYPE_MAIN 0x00
#define HID_ITEM_TYPE_GLOBAL 0x01
#define HID_ITEM_TYPE_LOCAL 0x02
#define HID_ITEM_TYPE_RESERVED 0x03

#define HID_MAIN_ITEM_INPUT 0x08
#define HID_MAIN_ITEM_OUTPUT 0x09
#define HID_MAIN_ITEM_COLLECTION 0x0a
#define HID_MAIN_ITEM_FEATURE 0x0b
#define HID_MAIN_ITEM_END_COLLECTION 0x0c

#define HID_GLOBAL_ITEM_USAGE_PAGE 0x00
#define HID_GLOBAL_ITEM_LOGICAL_MIN 0x01
#define HID_GLOBAL_ITEM_LOGICAL_MAX 0x02
#define HID_GLOBAL_ITEM_PHYSICAL_MIN 0x03
#define HID_GLOBAL_ITEM_PHYSICAL_MAX 0x04
#define HID_GLOBAL_ITEM_UNIT_EXPONENT 0x05
#define HID_GLOBAL_ITEM_UNIT 0x06
#define HID_GLOBAL_ITEM_REPORT_SIZE 0x07
#define HID_GLOBAL_ITEM_REPORT_ID 0x08
#define HID_GLOBAL_ITEM_REPORT_COUNT 0x09
#define HID_GLOBAL_ITEM_PUSH 0x0a
#define HID_GLOBAL_ITEM_POP 0x0b

#define HID_LOCAL_ITEM_USAGE 0x00
#define HID_LOCAL_ITEM_USAGE_MIN 0x01
#define HID_LOCAL_ITEM_USAGE_MAX 0x02
#define HID_LOCAL_ITEM_DESIGNATOR_INDEX 0x03
#define HID_LOCAL_ITEM_DESIGNATOR_MIN 0x04
#define HID_LOCAL_ITEM_DESIGNATOR_MAX 0x05
#define HID_LOCAL_ITEM_STRING_INDEX 0x07
#define HID_LOCAL_ITEM_STRING_MIN 0x08
#define HID_LOCAL_ITEM_STRING_MAX 0x09
#define HID_LOCAL_ITEM_DELIMITER 0x0a

#define HID_COLLECTION_PHYSICAL 0x00
#define HID_COLLECTION_APPLICATION 0x01
#define HID_COLLECTION_LOGICAL 0x02
#define HID_COLLECTION_REPORT 0x03
#define HID_COLLECTION_NAMED_ARRAY 0x04
#define HID_COLLECTION_USAGE_SWITCH 0x05
#define HID_COLLECTION_USAGE_MODIFIER 0x06

#define HID_GD_X 0x30
#define HID_GD_Y 0x31
#define HID_GD_Z 0x32
#define HID_GD_RX 0x33
#define HID_GD_RY 0x34
#define HID_GD_RZ 0x35
#define HID_GD_SLIDER 0x36
#define HID_GD_DIAL 0x37
#define HID_GD_WHEEL 0x38
#define HID_GD_HAT_SWITCH 0x39

#define HID_BUTTON_PRIMARY 0x01
#define HID_BUTTON_SECONDARY 0x02
#define HID_BUTTON_TERTIARY 0x03
#define HID_BUTTON_SIDE 0x04
#define HID_BUTTON_EXTRA 0x05
#define HID_BUTTON_MAX HID_BUTTON_EXTRA

#define HID_KEYBOARD_KEYPAD_MAX 0xe7

static inline void decode_item(uint8_t item, uint8_t *tag, uint8_t *type, uint8_t *size) {
	*size = item & 0x3;
	*type = (item >> 2) & 0x3;
	*tag = (item >> 4) & 0xf;
}

static inline uint32_t get_data_u(uint8_t *data, size_t size) {
	switch (size) {
		case 0:
			return 0;
		case 1:
			return data[0];
		case 2:
			return (uint32_t)data[0] |
			       ((uint32_t)data[1] << 8);
		case 4:
			return (uint32_t)data[0] |
			       ((uint32_t)data[1] << 8) |
			       ((uint32_t)data[2] << 16) |
			       ((uint32_t)data[3] << 24);
		default:
			__assert(!"Corruption in HID driver");
	}
}

static inline int32_t get_data_i(uint8_t *data, size_t size) {
	switch (size) {
		case 0:
			return 0;
		case 1:
			return (int32_t)(int8_t)data[0];
		case 2:
			return (int32_t)(int16_t)((uint16_t)data[0] |
						  ((uint16_t)data[1] << 8));
		case 4:
			return (int32_t)((uint32_t)data[0] |
					 ((uint32_t)data[1] << 8) |
					 ((uint32_t)data[2] << 16) |
					 ((uint32_t)data[3] << 24));
		default:
			__assert(!"Corruption in HID driver");
	}
}

static inline size_t translate_size(uint8_t size) {
	switch (size) {
		case 0:
		case 1:
		case 2:
			return size;
		case 3:
			return 4;
		default:
			__assert(!"Corruption in HID driver");
	}
}

typedef struct {
	uint32_t usage_page;
	int32_t logical_minimum;
       	int32_t logical_maximum;
	int32_t physical_minimum;
	int32_t physical_maximum;
	int32_t unit_exponent;
	uint32_t unit;
	uint32_t report_size;
	uint32_t report_id;
	uint32_t report_count;
} hid_global_state_t;

static int handle_global(hid_global_state_t *global_state, uint8_t tag, uint8_t *item, size_t actual_size) {
	uint32_t udata = get_data_u(item + 1, actual_size);
	int32_t idata = get_data_i(item + 1, actual_size);

	switch (tag) {
		case HID_GLOBAL_ITEM_USAGE_PAGE:
			global_state->usage_page = udata;
			break;
		case HID_GLOBAL_ITEM_LOGICAL_MIN:
			global_state->logical_minimum = idata;
			break;
		case HID_GLOBAL_ITEM_LOGICAL_MAX:
			global_state->logical_maximum = idata;
			break;
		case HID_GLOBAL_ITEM_PHYSICAL_MIN:
			global_state->physical_minimum = idata;
			break;
		case HID_GLOBAL_ITEM_PHYSICAL_MAX:
			global_state->physical_maximum = idata;
			break;
		case HID_GLOBAL_ITEM_UNIT_EXPONENT:
			global_state->unit_exponent = idata;
			break;
		case HID_GLOBAL_ITEM_UNIT:
			global_state->unit = udata;
			break;
		case HID_GLOBAL_ITEM_REPORT_SIZE:
			global_state->report_size = udata;
			break;
		case HID_GLOBAL_ITEM_REPORT_ID:
			if (udata == 0 || udata > 0xff)
				return EINVAL;

			global_state->report_id = udata;
			break;
		case HID_GLOBAL_ITEM_REPORT_COUNT:
			global_state->report_count = udata;
			break;
		default:
			return EINVAL;
	}

	return 0;
}
#define HID_LOCAL_USAGE_CAPACITY 128

typedef struct {
	hid_usage_t usages[HID_LOCAL_USAGE_CAPACITY];
	size_t usage_count;

	bool has_usage_minimum;
	uint32_t usage_minimum;

	bool has_designator_index;
	bool has_designator_minimum;
	bool has_designator_maximum;
	uint32_t designator_index;
	uint32_t designator_minimum;
	uint32_t designator_maximum;

	bool has_string_index;
	bool has_string_minimum;
	bool has_string_maximum;
	uint32_t string_index;
	uint32_t string_minimum;
	uint32_t string_maximum;
} hid_local_state_t;

static inline hid_usage_t hid_make_usage(uint32_t current_page, uint32_t raw_usage, size_t actual_size) {
	hid_usage_t usage = {
		.usage = actual_size <= 2 ? ((current_page << 16) | raw_usage) : raw_usage,

	};

	return usage;
}

static inline hid_usage_t hid_make_usage_range(hid_global_state_t *global_state, uint32_t min, uint32_t max) {
	hid_usage_t usage = {
		.minimum = min,
		.maximum = max,
		.usage = global_state->usage_page << 16
	};

	return usage;
}

static int hid_local_add_usage(hid_local_state_t *local_state, hid_usage_t usage) {
	if (local_state->usage_count >= HID_LOCAL_USAGE_CAPACITY)
		return E2BIG;

	local_state->usages[local_state->usage_count++] = usage;

	return 0;
}

static int handle_local(hid_global_state_t *global_state, hid_local_state_t *local_state, uint8_t tag, uint8_t *item, size_t actual_size) {
	uint32_t udata = get_data_u(item + 1, actual_size);

	switch (tag) {
		case HID_LOCAL_ITEM_USAGE: {
			hid_usage_t usage = hid_make_usage(global_state->usage_page, udata, actual_size);
			return hid_local_add_usage(local_state, usage);
		}
		case HID_LOCAL_ITEM_USAGE_MIN:
			local_state->has_usage_minimum = true;
			local_state->usage_minimum = udata;
			break;
		case HID_LOCAL_ITEM_USAGE_MAX:
			if (local_state->has_usage_minimum == false)
				return EINVAL;

			hid_usage_t usage = hid_make_usage_range(global_state, local_state->usage_minimum, udata);
			local_state->has_usage_minimum = false;
			return hid_local_add_usage(local_state, usage);
		case HID_LOCAL_ITEM_DESIGNATOR_INDEX:
			local_state->has_designator_index = true;
			local_state->designator_index = udata;
			break;
		case HID_LOCAL_ITEM_DESIGNATOR_MIN:
			local_state->has_designator_minimum = true;
			local_state->designator_minimum = udata;
			break;
		case HID_LOCAL_ITEM_DESIGNATOR_MAX:
			local_state->has_designator_maximum = true;
			local_state->designator_maximum = udata;
			break;
		case HID_LOCAL_ITEM_STRING_INDEX:
			local_state->has_string_index = true;
			local_state->string_index = udata;
			break;
		case HID_LOCAL_ITEM_STRING_MIN:
			local_state->has_string_minimum = true;
			local_state->string_minimum = udata;
			break;
		case HID_LOCAL_ITEM_STRING_MAX:
			local_state->has_string_maximum = true;
			local_state->string_maximum = udata;
			break;
		case HID_LOCAL_ITEM_DELIMITER:
			return ENOTSUP;
	}

	return 0;
}

typedef struct {
	unsigned int current_depth;
} hid_collection_state_t;

static int handle_collection(hid_parser_t *parser, hid_collection_state_t *collection_state, hid_global_state_t *global_state, hid_local_state_t *local_state, uint8_t type) {
	// only accept root application collections
	if (collection_state->current_depth == 0 && type != HID_COLLECTION_APPLICATION)
		return EINVAL;

	// currently ignore all non-root collections
	if (collection_state->current_depth++ != 0)
		return 0;

	// check if we have valid usage state before proceeding
	if (local_state->usage_count != 1)
		return EINVAL;

	// add a new application to the parser
	void *p;
	if (parser->applications == NULL)
		p = alloc(sizeof(hid_application_t));
	else
		p = realloc(parser->applications, (parser->application_count + 1) * sizeof(hid_application_t));

	if (p == NULL)
		return ENOMEM;

	parser->applications = p;
	parser->applications[parser->application_count].usage = local_state->usages[0].usage;
	++parser->application_count;
	return 0;
}

static int handle_collection_end(hid_collection_state_t *collection_state) {
	if (collection_state->current_depth == 0)
		return EINVAL;

	--collection_state->current_depth;

	return 0;
}

static int insert_report(hid_parser_t *parser, unsigned int id, hid_report_t **report) {
	void *p = parser->reports ? realloc(parser->reports, sizeof(hid_report_t) * (parser->report_count + 1)) : alloc(sizeof(hid_report_t));
	if (p == NULL)
		return ENOMEM;

	parser->reports = p;
	*report = &parser->reports[parser->report_count++];

	(*report)->report_id = id;
	return 0;
}

static int insert_input(hid_parser_t *parser, hid_input_t **inputp, size_t *input_count, size_t *report_bit_size, hid_collection_state_t *collection_state, hid_global_state_t *global_state, hid_local_state_t *local_state, uint16_t flags) {
	size_t field_bit_size = global_state->report_size * global_state->report_count;
	if (global_state->report_count && field_bit_size / global_state->report_count != global_state->report_size)
		return EOVERFLOW;
	if (*report_bit_size + field_bit_size < *report_bit_size)
		return EOVERFLOW;

	void *p = *inputp ? realloc(*inputp, sizeof(hid_input_t) * (*input_count + 1)) : alloc(sizeof(hid_input_t));
	if (p == NULL)
		return ENOMEM;

	*inputp = p;
	hid_input_t *input = &(*inputp)[(*input_count)++];
	input->application_id = parser->application_count - 1;
	input->bit_offset = *report_bit_size;
	if (local_state->usage_count) {
		input->usages = alloc(local_state->usage_count * sizeof(hid_usage_t));
		if (input->usages == NULL) {
			// TODO proper cleanup here
			return ENOMEM;
		}
		memcpy(input->usages, local_state->usages, local_state->usage_count * sizeof(hid_usage_t));
		input->usage_count = local_state->usage_count;
	}
	input->logical_maximum = global_state->logical_maximum;
	input->logical_minimum = global_state->logical_minimum;
	input->bit_size = global_state->report_size;
	input->count = global_state->report_count;
	input->flags = flags;
	*report_bit_size += field_bit_size;

	return 0;
}

static int handle_input(hid_parser_t *parser, hid_collection_state_t *collection_state, hid_global_state_t *global_state, hid_local_state_t *local_state, uint16_t flags) {
	if (collection_state->current_depth == 0)
		return EINVAL;

	if (global_state->report_id == 0) {
		// add it to the input list without being gated behind a report id
		if (parser->report_count)
			return EINVAL;

		return insert_input(parser, &parser->inputs, &parser->input_count, &parser->input_bit_size, collection_state, global_state, local_state, flags);
	}

	if (parser->input_count)
		return EINVAL;

	hid_report_t *report = hid_get_report_from_id(parser, global_state->report_id);
	if (report == NULL) {
		// we are guaranteed to have a valid report_id here
		int error = insert_report(parser, global_state->report_id, &report);
		if (error)
			return error;
	}

	return insert_input(parser, &report->inputs, &report->input_count, &report->bit_size, collection_state, global_state, local_state, flags);
}

static int handle_main(hid_parser_t *parser, hid_collection_state_t *collection_state, hid_global_state_t *global_state, hid_local_state_t *local_state, uint8_t tag, uint8_t *item, size_t actual_size) {
	uint32_t udata = get_data_u(item + 1, actual_size);

	int error = 0;
	switch (tag) {
		case HID_MAIN_ITEM_INPUT:
			error = handle_input(parser, collection_state, global_state, local_state, udata);
			break;
		case HID_MAIN_ITEM_COLLECTION:
			error = handle_collection(parser, collection_state, global_state, local_state, udata);
			break;
		case HID_MAIN_ITEM_END_COLLECTION:
			error = handle_collection_end(collection_state);
			break;
	}

	memset(local_state, 0, sizeof(hid_local_state_t));
	return error;
}

int hid_parser_init(hid_parser_t *parser, void *data, size_t data_size) {
	uint8_t *item = data;
	size_t current_offset = 0;
	uint8_t tag, type, size;

	hid_global_state_t globals = {0};
	hid_local_state_t locals = {0};
	hid_collection_state_t collection_state = {0};

	// TODO support PUSH and POP operations
	while (current_offset < data_size) {
		// TODO: if item 0xfe, it is a long item and will be ignored for now.
		decode_item(item[current_offset], &tag, &type, &size);
		size_t actual_size = translate_size(size);
		if (current_offset + 1 + actual_size > data_size) {
			printf("hid: short item exceeds report descriptor length\n");
			return EINVAL;
		}

		int error = 0;
		switch (type) {
			case HID_ITEM_TYPE_GLOBAL:
				error = handle_global(&globals, tag, &item[current_offset], actual_size);
				break;
			case HID_ITEM_TYPE_LOCAL:
				error = handle_local(&globals, &locals, tag, &item[current_offset], actual_size);
				break;
			case HID_ITEM_TYPE_MAIN:
				error = handle_main(parser, &collection_state, &globals, &locals, tag, &item[current_offset], actual_size);
				break;
		}

		if (error) {
			// TODO eventual cleanup
			printf("hid: parsing returned an error: %s\n", strerror(error));
			return error;
		}

		current_offset += 1 + actual_size;
	}

	if (collection_state.current_depth != 0) {
		printf("hid: unclosed collection in report descriptor\n");
		return EINVAL;
	}

	return 0;
}

#define EVENT_EMISSION_LIMIT 32

static const uint16_t hid_keyboard_keypad_to_input_key_table[HID_KEYBOARD_KEYPAD_MAX + 1] = {
	[0x04] = INPUT_KEY_A,
	[0x05] = INPUT_KEY_B,
	[0x06] = INPUT_KEY_C,
	[0x07] = INPUT_KEY_D,
	[0x08] = INPUT_KEY_E,
	[0x09] = INPUT_KEY_F,
	[0x0a] = INPUT_KEY_G,
	[0x0b] = INPUT_KEY_H,
	[0x0c] = INPUT_KEY_I,
	[0x0d] = INPUT_KEY_J,
	[0x0e] = INPUT_KEY_K,
	[0x0f] = INPUT_KEY_L,
	[0x10] = INPUT_KEY_M,
	[0x11] = INPUT_KEY_N,
	[0x12] = INPUT_KEY_O,
	[0x13] = INPUT_KEY_P,
	[0x14] = INPUT_KEY_Q,
	[0x15] = INPUT_KEY_R,
	[0x16] = INPUT_KEY_S,
	[0x17] = INPUT_KEY_T,
	[0x18] = INPUT_KEY_U,
	[0x19] = INPUT_KEY_V,
	[0x1a] = INPUT_KEY_W,
	[0x1b] = INPUT_KEY_X,
	[0x1c] = INPUT_KEY_Y,
	[0x1d] = INPUT_KEY_Z,
	[0x1e] = INPUT_KEY_1,
	[0x1f] = INPUT_KEY_2,
	[0x20] = INPUT_KEY_3,
	[0x21] = INPUT_KEY_4,
	[0x22] = INPUT_KEY_5,
	[0x23] = INPUT_KEY_6,
	[0x24] = INPUT_KEY_7,
	[0x25] = INPUT_KEY_8,
	[0x26] = INPUT_KEY_9,
	[0x27] = INPUT_KEY_0,
	[0x28] = INPUT_KEY_ENTER,
	[0x29] = INPUT_KEY_ESC,
	[0x2a] = INPUT_KEY_BACKSPACE,
	[0x2b] = INPUT_KEY_TAB,
	[0x2c] = INPUT_KEY_SPACE,
	[0x2d] = INPUT_KEY_MINUS,
	[0x2e] = INPUT_KEY_EQUAL,
	[0x2f] = INPUT_KEY_LEFTBRACE,
	[0x30] = INPUT_KEY_RIGHTBRACE,
	[0x31] = INPUT_KEY_BACKSLASH,
	[0x33] = INPUT_KEY_SEMICOLON,
	[0x34] = INPUT_KEY_APOSTROPHE,
	[0x35] = INPUT_KEY_GRAVE,
	[0x36] = INPUT_KEY_COMMA,
	[0x37] = INPUT_KEY_DOT,
	[0x38] = INPUT_KEY_SLASH,
	[0x39] = INPUT_KEY_CAPSLOCK,
	[0x3a] = INPUT_KEY_F1,
	[0x3b] = INPUT_KEY_F2,
	[0x3c] = INPUT_KEY_F3,
	[0x3d] = INPUT_KEY_F4,
	[0x3e] = INPUT_KEY_F5,
	[0x3f] = INPUT_KEY_F6,
	[0x40] = INPUT_KEY_F7,
	[0x41] = INPUT_KEY_F8,
	[0x42] = INPUT_KEY_F9,
	[0x43] = INPUT_KEY_F10,
	[0x44] = INPUT_KEY_F11,
	[0x45] = INPUT_KEY_F12,
	[0x47] = INPUT_KEY_SCROLLLOCK,
	[0x49] = INPUT_KEY_INSERT,
	[0x4a] = INPUT_KEY_HOME,
	[0x4b] = INPUT_KEY_PAGEUP,
	[0x4c] = INPUT_KEY_DELETE,
	[0x4d] = INPUT_KEY_END,
	[0x4e] = INPUT_KEY_PAGEDOWN,
	[0x4f] = INPUT_KEY_RIGHT,
	[0x50] = INPUT_KEY_LEFT,
	[0x51] = INPUT_KEY_DOWN,
	[0x52] = INPUT_KEY_UP,
	[0x53] = INPUT_KEY_NUMLOCK,
	[0x54] = INPUT_KEY_KPSLASH,
	[0x55] = INPUT_KEY_KPASTERISK,
	[0x56] = INPUT_KEY_KPMINUS,
	[0x57] = INPUT_KEY_KPPLUS,
	[0x58] = INPUT_KEY_KPENTER,
	[0x59] = INPUT_KEY_KP1,
	[0x5a] = INPUT_KEY_KP2,
	[0x5b] = INPUT_KEY_KP3,
	[0x5c] = INPUT_KEY_KP4,
	[0x5d] = INPUT_KEY_KP5,
	[0x5e] = INPUT_KEY_KP6,
	[0x5f] = INPUT_KEY_KP7,
	[0x60] = INPUT_KEY_KP8,
	[0x61] = INPUT_KEY_KP9,
	[0x62] = INPUT_KEY_KP0,
	[0x63] = INPUT_KEY_KPDOT,
	[0x68] = INPUT_KEY_F13,
	[0x69] = INPUT_KEY_F14,
	[0x6a] = INPUT_KEY_F15,
	[0x6b] = INPUT_KEY_F16,
	[0x6c] = INPUT_KEY_F17,
	[0x6d] = INPUT_KEY_F18,
	[0x6e] = INPUT_KEY_F19,
	[0x6f] = INPUT_KEY_F20,
	[0x70] = INPUT_KEY_F21,
	[0x71] = INPUT_KEY_F22,
	[0x72] = INPUT_KEY_F23,
	[0x73] = INPUT_KEY_F24,
	[0xe0] = INPUT_KEY_LEFTCTRL,
	[0xe1] = INPUT_KEY_LEFTSHIFT,
	[0xe2] = INPUT_KEY_LEFTALT,
	[0xe3] = INPUT_KEY_LEFTMETA,
	[0xe4] = INPUT_KEY_RIGHTCTRL,
	[0xe5] = INPUT_KEY_RIGHTSHIFT,
	[0xe6] = INPUT_KEY_RIGHTALT,
	[0xe7] = INPUT_KEY_RIGHTMETA
};

static uint16_t hid_keyboard_keypad_to_input_key(uint32_t hid_key) {
	if (hid_key > HID_KEYBOARD_KEYPAD_MAX)
		return INPUT_KEY_RESERVED;

	return hid_keyboard_keypad_to_input_key_table[hid_key];
}

static void push_event(input_event_t event, input_event_t *events, size_t *events_emitted) {
	if (*events_emitted >= EVENT_EMISSION_LIMIT)
		return;

	events[*events_emitted] = event;
	*events_emitted += 1;
}

static inline void hid_key_set(uint64_t *keys, uint32_t usage) {
	if (usage > HID_KEYBOARD_KEYPAD_MAX)
		return;

	keys[usage / 64] |= 1ull << (usage % 64);
}

static inline bool hid_key_is_modifier(uint32_t usage) {
	return usage >= 0xe0 && usage <= 0xe7;
}

static inline int64_t hid_input_value_signed(hid_input_t *input, uint64_t value) {
	if (input->logical_minimum >= 0 || input->bit_size == 0 || input->bit_size >= 64)
		return (int64_t)value;

	uint64_t sign_bit = 1ull << (input->bit_size - 1);
	if (value & sign_bit)
		value |= ~((1ull << input->bit_size) - 1);

	return (int64_t)value;
}

static void hid_emit_event_keyboard_keypad(hid_application_t *application, hid_input_t *input, hid_usage_t *usage, size_t idx, uint64_t value, input_event_t *events, size_t *events_emitted) {
	if (input->flags & HID_INPUT_CONSTANT)
		return;

	if (input->flags & HID_INPUT_VARIABLE) {
		if (value == 0)
			return;

		if (usage->minimum || usage->maximum) {
			value = usage->minimum + idx;
			if (value > usage->maximum)
				return;
		} else {
			value = USAGE(usage->usage);
		}
	}

	hid_key_set(application->pressed_keys, value);
}

static uint16_t hid_generic_desktop_to_input_rel(uint32_t usage);

static void hid_emit_event_generic_desktop(hid_input_t *input, hid_usage_t *usage, size_t idx, uint64_t value, input_event_t *events, size_t *events_emitted) {
	if (input->flags & HID_INPUT_CONSTANT)
		return;

	if ((input->flags & HID_INPUT_RELATIVE) == 0)
		return;

	uint32_t hid_usage = USAGE(usage->usage);
	if (usage->minimum || usage->maximum) {
		hid_usage = usage->minimum + idx;
		if (hid_usage > usage->maximum)
			return;
	}

	uint16_t code = hid_generic_desktop_to_input_rel(hid_usage);
	if (code > INPUT_REL_MAX)
		return;

	int64_t signed_value = hid_input_value_signed(input, value);
	if (signed_value == 0)
		return;

	input_event_t event = {0};
	event.type = INPUT_EV_REL;
	event.code = code;
	event.value = signed_value;
	push_event(event, events, events_emitted);
}

static inline void hid_button_set(hid_application_t *application, uint32_t button) {
	if (button < HID_BUTTON_PRIMARY || button > HID_BUTTON_MAX)
		return;

	application->pressed_buttons |= 1ull << (button - HID_BUTTON_PRIMARY);
}

static void hid_emit_event_button(hid_application_t *application, hid_input_t *input, hid_usage_t *usage, size_t idx, uint64_t value) {
	if (input->flags & HID_INPUT_CONSTANT)
		return;

	if (input->flags & HID_INPUT_VARIABLE) {
		if (value == 0)
			return;

		if (usage->minimum || usage->maximum) {
			uint32_t button = usage->minimum + idx;
			if (button > usage->maximum)
				return;

			hid_button_set(application, button);
		} else {
			hid_button_set(application, USAGE(usage->usage));
		}
	} else if (value) {
		hid_button_set(application, value);
	}
}

static void hid_emit_event(hid_application_t *application, hid_input_t *input, hid_usage_t *usage, size_t idx, uint64_t value, input_event_t *events, size_t *events_emitted) {
	switch (USAGE_PAGE(usage->usage)) {
		case HID_USAGE_PAGE_GENERIC_DESKTOP:
			hid_emit_event_generic_desktop(input, usage, idx, value, events, events_emitted);
			break;
		case HID_USAGE_PAGE_KEYBOARD_KEYPAD:
			hid_emit_event_keyboard_keypad(application, input, usage, idx, value, events, events_emitted);
			break;
		case HID_USAGE_PAGE_BUTTON:
			hid_emit_event_button(application, input, usage, idx, value);
			break;
	}
}

static inline uint64_t get_bitfield_data_at_offset(uint8_t *data, size_t bit_count, size_t bit_offset) {
	uint64_t value = 0;

	if (bit_count == 0)
		return 0;

	__assert(bit_count <= 64);

	for (size_t i = 0; i < bit_count; i++) {
		size_t src_bit = bit_offset + i;
		uint8_t byte = data[src_bit / 8];
		uint8_t bit = (byte >> (src_bit % 8)) & 1;

		value |= (uint64_t)bit << i;
	}

	return value;
}

static void do_input(hid_parser_t *parser, hid_input_t *input, uint8_t *data) {
	hid_application_t *application = &parser->applications[input->application_id];

	if (input->bit_size > 64) {
		printf("hid: bit sizes above 64-bit are currently a TODO.\n");
		return;
	}

	input_event_t events[EVENT_EMISSION_LIMIT];
	size_t events_emitted = 0;
	hid_usage_t *usage = NULL;
	for (size_t i = 0; i < input->count; ++i) {
		if (i < input->usage_count)
			usage = &input->usages[i];

		// no point in handling this if we are not gonna have an use for it
		if (usage == NULL)
			break;

		uint64_t value = get_bitfield_data_at_offset(data, input->bit_size, input->bit_offset + input->bit_size * i);
		hid_emit_event(application, input, usage, i, value, events, &events_emitted);
	}

	if (events_emitted)
		input_queue_packet(application->input_device, events, events_emitted);
}

typedef struct {
	hid_input_t *inputs;
	size_t input_count;
	unsigned int next_input;
} parse_state_t;

static bool consume_next_input(parse_state_t *parse_state, hid_input_t **current_input) {
	if (parse_state->next_input >= parse_state->input_count)
		return false;

	*current_input = &parse_state->inputs[parse_state->next_input];

	++parse_state->next_input;
	return true;
}

static void hid_handle_key_changes(hid_application_t *application, input_event_t *events, size_t *events_emitted, uint64_t old_keys, size_t offset, bool modifiers) {
	uint64_t changes = old_keys ^ application->pressed_keys[offset];
	while (changes) {
		int bit = __builtin_ctzll(changes);
		uint32_t value = offset * 64 + bit;

		if (hid_key_is_modifier(value) != modifiers) {
			changes &= ~(1llu << bit);
			continue;
		}

		input_event_t event = {0};
		event.type = INPUT_EV_KEY;
		event.code = hid_keyboard_keypad_to_input_key(value);
		if (event.code != INPUT_KEY_RESERVED) {
			event.value = (application->pressed_keys[offset] & (1ull << bit)) ? 1 : 0;
			push_event(event, events, events_emitted);
		}

		changes &= ~(1llu << bit);
	}
}

static uint16_t hid_button_to_input_key(uint32_t usage);

static void hid_handle_button_changes(hid_application_t *application, input_event_t *events, size_t *events_emitted, uint64_t old_buttons) {
	uint64_t changes = old_buttons ^ application->pressed_buttons;
	while (changes) {
		int bit = __builtin_ctzll(changes);
		uint32_t button = HID_BUTTON_PRIMARY + bit;

		input_event_t event = {0};
		event.type = INPUT_EV_KEY;
		event.code = hid_button_to_input_key(button);
		if (event.code != INPUT_KEY_RESERVED) {
			event.value = (application->pressed_buttons & (1ull << bit)) ? 1 : 0;
			push_event(event, events, events_emitted);
		}

		changes &= ~(1llu << bit);
	}
}

static void do_report_parse(hid_parser_t *parser, hid_input_t *inputs, size_t input_count, uint8_t *data) {
	parse_state_t parse_state = {
		.inputs = inputs,
		.input_count = input_count
	};

	hid_input_t *current_input;

	__assert(parser->application_count <= 64);
	uint64_t applications_handled = 0;
	uint64_t old_keys[4 * parser->application_count];
	uint64_t old_buttons[parser->application_count];
	while (consume_next_input(&parse_state, &current_input)) {
		if ((applications_handled & (1llu << current_input->application_id)) == 0) {
			memcpy(&old_keys[current_input->application_id * 4], parser->applications[current_input->application_id].pressed_keys, sizeof(parser->applications[current_input->application_id].pressed_keys));
			memset(parser->applications[current_input->application_id].pressed_keys, 0, sizeof(parser->applications[current_input->application_id].pressed_keys));
			old_buttons[current_input->application_id] = parser->applications[current_input->application_id].pressed_buttons;
			parser->applications[current_input->application_id].pressed_buttons = 0;
			applications_handled |= (1llu << current_input->application_id);
		}

		do_input(parser, current_input, data);
	}

	for (size_t i = 0; i < parser->application_count; ++i) {
		if ((applications_handled & (1llu << i)) == 0)
			continue;

		input_event_t events[EVENT_EMISSION_LIMIT];
		size_t events_emitted = 0;

		for (int j = 0; j < 4; ++j)
			if (old_keys[i * 4 + j] ^ parser->applications[i].pressed_keys[j])
				hid_handle_key_changes(&parser->applications[i], events, &events_emitted, old_keys[i * 4 + j], j, true);

		for (int j = 0; j < 4; ++j)
			if (old_keys[i * 4 + j] ^ parser->applications[i].pressed_keys[j])
				hid_handle_key_changes(&parser->applications[i], events, &events_emitted, old_keys[i * 4 + j], j, false);

		if (old_buttons[i] ^ parser->applications[i].pressed_buttons)
			hid_handle_button_changes(&parser->applications[i], events, &events_emitted, old_buttons[i]);

		if (events_emitted)
			input_queue_packet(parser->applications[i].input_device, events, events_emitted);
	}
}

void hid_parse_report(hid_parser_t *parser, hid_report_t *report, uint8_t *data, size_t size) {
	if (report) {
		// separated by reports
		if (ROUND_UP(report->bit_size, 8) / 8 > size)
			return;

		do_report_parse(parser, report->inputs, report->input_count, data);
	} else {
		// only anonymous inputs
		if (ROUND_UP(parser->input_bit_size, 8) / 8 > size)
			return;

		do_report_parse(parser, parser->inputs, parser->input_count, data);
	}
}

static void hid_advertise_keyboard_keypad(input_device_t *device, hid_input_t *input, hid_usage_t *usage) {
	if (input->flags & HID_INPUT_CONSTANT)
		return;

	if (usage->minimum == 0 && usage->maximum == 0) {
		uint16_t code = hid_keyboard_keypad_to_input_key(USAGE(usage->usage));
		if (code != INPUT_KEY_RESERVED)
			bitmap_set(&device->key_bits, code, 1);
	}

	for (uint32_t hid_key = usage->minimum; hid_key <= usage->maximum && hid_key <= HID_KEYBOARD_KEYPAD_MAX; ++hid_key) {
		uint16_t code = hid_keyboard_keypad_to_input_key(hid_key);
		if (code != INPUT_KEY_RESERVED)
			bitmap_set(&device->key_bits, code, 1);
	}
}

static uint16_t hid_generic_desktop_to_input_rel(uint32_t usage) {
	switch (usage) {
		case HID_GD_X:
			return INPUT_REL_X;
		case HID_GD_Y:
			return INPUT_REL_Y;
		case HID_GD_WHEEL:
			return INPUT_REL_WHEEL;
		default:
			return INPUT_REL_MAX + 1;
	}
}

static void hid_advertise_generic_desktop(input_device_t *device, hid_input_t *input, hid_usage_t *usage) {
	if (input->flags & HID_INPUT_CONSTANT)
		return;

	if ((input->flags & HID_INPUT_RELATIVE) == 0)
		return;

	if (usage->minimum == 0 && usage->maximum == 0) {
		uint16_t code = hid_generic_desktop_to_input_rel(USAGE(usage->usage));
		if (code <= INPUT_REL_MAX) {
			bitmap_set(&device->ev_bits, INPUT_EV_REL, 1);
			bitmap_set(&device->rel_bits, code, 1);
		}
	}

	for (uint32_t hid_usage = usage->minimum; hid_usage <= usage->maximum && hid_usage <= HID_GD_WHEEL; ++hid_usage) {
		uint16_t code = hid_generic_desktop_to_input_rel(hid_usage);
		if (code <= INPUT_REL_MAX) {
			bitmap_set(&device->ev_bits, INPUT_EV_REL, 1);
			bitmap_set(&device->rel_bits, code, 1);
		}
	}
}

static uint16_t hid_button_to_input_key(uint32_t usage) {
	switch (usage) {
		case HID_BUTTON_PRIMARY:
			return INPUT_KEY_BTN_LEFT;
		case HID_BUTTON_SECONDARY:
			return INPUT_KEY_BTN_RIGHT;
		case HID_BUTTON_TERTIARY:
			return INPUT_KEY_BTN_MIDDLE;
		case HID_BUTTON_SIDE:
			return INPUT_KEY_BTN_SIDE;
		case HID_BUTTON_EXTRA:
			return INPUT_KEY_BTN_EXTRA;
		default:
			return INPUT_KEY_RESERVED;
	}
}

static void hid_advertise_button(input_device_t *device, hid_input_t *input, hid_usage_t *usage) {
	if (input->flags & HID_INPUT_CONSTANT)
		return;

	if (usage->minimum == 0 && usage->maximum == 0) {
		uint16_t code = hid_button_to_input_key(USAGE(usage->usage));
		if (code != INPUT_KEY_RESERVED) {
			bitmap_set(&device->ev_bits, INPUT_EV_KEY, 1);
			bitmap_set(&device->key_bits, code, 1);
		}
	}

	for (uint32_t button = usage->minimum; button <= usage->maximum && button <= HID_BUTTON_MAX; ++button) {
		uint16_t code = hid_button_to_input_key(button);
		if (code != INPUT_KEY_RESERVED) {
			bitmap_set(&device->ev_bits, INPUT_EV_KEY, 1);
			bitmap_set(&device->key_bits, code, 1);
		}
	}
}

static void hid_advertise_input(hid_parser_t *parser, hid_input_t *input) {
	hid_application_t *application = &parser->applications[input->application_id];
	input_device_t *device = application->input_device;

	for (size_t i = 0; i < input->usage_count; ++i) {
		hid_usage_t *usage = &input->usages[i];
		switch (USAGE_PAGE(usage->usage)) {
			case HID_USAGE_PAGE_GENERIC_DESKTOP:
				hid_advertise_generic_desktop(device, input, usage);
				break;
			case HID_USAGE_PAGE_KEYBOARD_KEYPAD:
				bitmap_set(&device->ev_bits, INPUT_EV_KEY, 1);
				hid_advertise_keyboard_keypad(device, input, usage);
				break;
			case HID_USAGE_PAGE_BUTTON:
				hid_advertise_button(device, input, usage);
				break;
		}
	}
}

void hid_advertise_events(hid_parser_t *parser) {
	if (parser->report_count) {
		for (size_t i = 0; i < parser->report_count; ++i) {
			hid_report_t *report = &parser->reports[i];
			for (size_t j = 0; j < report->input_count; ++j)
				hid_advertise_input(parser, &report->inputs[j]);
		}
	} else {
		for (size_t i = 0; i < parser->input_count; ++i)
			hid_advertise_input(parser, &parser->inputs[i]);
	}
}
