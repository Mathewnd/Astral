#ifndef _USB_HID_H
#define _USB_HID_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/input.h>

#define HID_USAGE_PAGE_GENERIC_DESKTOP 0x01
#define HID_USAGE_PAGE_SIMULATION 0x02
#define HID_USAGE_PAGE_VR 0x03
#define HID_USAGE_PAGE_SPORT 0x04
#define HID_USAGE_PAGE_GAME 0x05
#define HID_USAGE_PAGE_GENERIC_DEVICE 0x06
#define HID_USAGE_PAGE_KEYBOARD_KEYPAD 0x07
#define HID_USAGE_PAGE_LED 0x08
#define HID_USAGE_PAGE_BUTTON 0x09
#define HID_USAGE_PAGE_ORDINAL 0x0a
#define HID_USAGE_PAGE_TELEPHONY 0x0b
#define HID_USAGE_PAGE_CONSUMER 0x0c
#define HID_USAGE_PAGE_DIGITIZER 0x0d
#define HID_USAGE_PAGE_PID 0x0f
#define HID_USAGE_PAGE_UNICODE 0x10
#define HID_USAGE_PAGE_SENSOR 0x20
#define HID_USAGE_PAGE_VENDOR_MIN 0xff00
#define HID_USAGE_PAGE_VENDOR_MAX 0xffff

#define HID_GD_POINTER 0x01
#define HID_GD_MOUSE 0x02
#define HID_GD_JOYSTICK 0x04
#define HID_GD_GAMEPAD 0x05
#define HID_GD_KEYBOARD 0x06
#define HID_GD_KEYPAD 0x07
#define HID_GD_MULTI_AXIS_CONTROLLER 0x08

typedef struct {
	uint32_t usage;
	uint32_t minimum;
	uint32_t maximum;
} hid_usage_t;

#define USAGE_PAGE(x) (((x) >> 16) & 0xffff)
#define USAGE(x) ((x) & 0xffff)

#define HID_INPUT_CONSTANT (1u << 0)
#define HID_INPUT_VARIABLE (1u << 1)
#define HID_INPUT_RELATIVE (1u << 2)
#define HID_INPUT_WRAP (1u << 3)
#define HID_INPUT_NON_LINEAR (1u << 4)
#define HID_INPUT_NO_PREFERRED (1u << 5)
#define HID_INPUT_NULL_STATE (1u << 6)
#define HID_INPUT_VOLATILE (1u << 7)
#define HID_INPUT_BUFFERED_BYTES (1u << 8)

typedef struct {
	unsigned int application_id;

	hid_usage_t *usages;
	size_t usage_count;
	long logical_maximum;
	long logical_minimum;
	uint16_t flags;

	size_t bit_offset;
	size_t bit_size;
	size_t count;
} hid_input_t;

typedef struct {
	unsigned int report_id;
	hid_input_t *inputs;
	size_t input_count;
	size_t bit_size;
} hid_report_t;

typedef struct {
	uint32_t usage;
	input_device_t *input_device;
	uint64_t pressed_keys[4];
	uint64_t pressed_buttons;
} hid_application_t;

typedef struct {
	union {
		hid_report_t *reports;
		hid_input_t *inputs;
	};
	size_t report_count;
	size_t input_count;
	size_t input_bit_size;

	hid_application_t *applications;
	size_t application_count;
} hid_parser_t;

int hid_parser_init(hid_parser_t *parser, void *data, size_t size);
hid_report_t *hid_get_report_from_id(hid_parser_t *parser, unsigned int report_id);
void hid_parse_report(hid_parser_t *parser, hid_report_t *report, uint8_t *data, size_t size);
void hid_advertise_events(hid_parser_t *parser);

#endif
