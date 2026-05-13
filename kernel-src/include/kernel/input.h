#ifndef _INPUT_H
#define _INPUT_H

#include <kernel/poll.h>
#include <kernel/timekeeper.h>
#include <kernel/vfs.h>
#include <bitmap.h>
#include <list.h>
#include <ringbuffer.h>
#include <semaphore.h>
#include <spinlock.h>

typedef struct {
	timeval_t time;
	uint16_t type;
	uint16_t code;
	int32_t value;
} input_event_t;

typedef struct {
	int32_t value;
	int32_t min;
	int32_t max;
	int32_t fuzz;
	int32_t flat;
	int32_t res;
} input_absinfo_t;

#define INPUT_DEVICE_BUS_PCI 0x1
#define INPUT_DEVICE_BUS_USB 0x3
#define INPUT_DEVICE_BUS_VIRTUAL 0x6
#define INPUT_DEVICE_BUS_I8042 0x11

#define INPUT_PROP_POINTER 0x0
#define INPUT_PROP_DIRECT 0x1
#define INPUT_PROP_MAX 0x1f
#define INPUT_PROP_CNT (INPUT_PROP_MAX + 1)

#define INPUT_EV_SYN 0x0
#define INPUT_EV_KEY 0x1
#define INPUT_EV_REL 0x2
#define INPUT_EV_ABS 0x3
#define INPUT_EV_MSC 0x4
#define INPUT_EV_MAX 0x1f
#define INPUT_EV_CNT (INPUT_EV_MAX + 1)

#define INPUT_SYN_REPORT 0x0
#define INPUT_SYN_DROPPED 0x3
#define INPUT_SYN_MAX 0xf
#define INPUT_SYN_CNT (INPUT_SYN_MAX + 1)

#define INPUT_KEY_RESERVED 0x0
#define INPUT_KEY_ESC 0x1
#define INPUT_KEY_1 0x2
#define INPUT_KEY_2 0x3
#define INPUT_KEY_3 0x4
#define INPUT_KEY_4 0x5
#define INPUT_KEY_5 0x6
#define INPUT_KEY_6 0x7
#define INPUT_KEY_7 0x8
#define INPUT_KEY_8 0x9
#define INPUT_KEY_9 0xa
#define INPUT_KEY_0 0xb
#define INPUT_KEY_MINUS 0xc
#define INPUT_KEY_EQUAL 0xd
#define INPUT_KEY_BACKSPACE 0xe
#define INPUT_KEY_TAB 0xf
#define INPUT_KEY_Q 0x10
#define INPUT_KEY_W 0x11
#define INPUT_KEY_E 0x12
#define INPUT_KEY_R 0x13
#define INPUT_KEY_T 0x14
#define INPUT_KEY_Y 0x15
#define INPUT_KEY_U 0x16
#define INPUT_KEY_I 0x17
#define INPUT_KEY_O 0x18
#define INPUT_KEY_P 0x19
#define INPUT_KEY_LEFTBRACE 0x1a
#define INPUT_KEY_RIGHTBRACE 0x1b
#define INPUT_KEY_ENTER 0x1c
#define INPUT_KEY_LEFTCTRL 0x1d
#define INPUT_KEY_A 0x1e
#define INPUT_KEY_S 0x1f
#define INPUT_KEY_D 0x20
#define INPUT_KEY_F 0x21
#define INPUT_KEY_G 0x22
#define INPUT_KEY_H 0x23
#define INPUT_KEY_J 0x24
#define INPUT_KEY_K 0x25
#define INPUT_KEY_L 0x26
#define INPUT_KEY_SEMICOLON 0x27
#define INPUT_KEY_APOSTROPHE 0x28
#define INPUT_KEY_GRAVE 0x29
#define INPUT_KEY_LEFTSHIFT 0x2a
#define INPUT_KEY_BACKSLASH 0x2b
#define INPUT_KEY_Z 0x2c
#define INPUT_KEY_X 0x2d
#define INPUT_KEY_C 0x2e
#define INPUT_KEY_V 0x2f
#define INPUT_KEY_B 0x30
#define INPUT_KEY_N 0x31
#define INPUT_KEY_M 0x32
#define INPUT_KEY_COMMA 0x33
#define INPUT_KEY_DOT 0x34
#define INPUT_KEY_SLASH 0x35
#define INPUT_KEY_RIGHTSHIFT 0x36
#define INPUT_KEY_KPASTERISK 0x37
#define INPUT_KEY_LEFTALT 0x38
#define INPUT_KEY_SPACE 0x39
#define INPUT_KEY_CAPSLOCK 0x3a
#define INPUT_KEY_F1 0x3b
#define INPUT_KEY_F2 0x3c
#define INPUT_KEY_F3 0x3d
#define INPUT_KEY_F4 0x3e
#define INPUT_KEY_F5 0x3f
#define INPUT_KEY_F6 0x40
#define INPUT_KEY_F7 0x41
#define INPUT_KEY_F8 0x42
#define INPUT_KEY_F9 0x43
#define INPUT_KEY_F10 0x44
#define INPUT_KEY_NUMLOCK 0x45
#define INPUT_KEY_SCROLLLOCK 0x46
#define INPUT_KEY_KP7 0x47
#define INPUT_KEY_KP8 0x48
#define INPUT_KEY_KP9 0x49
#define INPUT_KEY_KPMINUS 0x4a
#define INPUT_KEY_KP4 0x4b
#define INPUT_KEY_KP5 0x4c
#define INPUT_KEY_KP6 0x4d
#define INPUT_KEY_KPPLUS 0x4e
#define INPUT_KEY_KP1 0x4f
#define INPUT_KEY_KP2 0x50
#define INPUT_KEY_KP3 0x51
#define INPUT_KEY_KP0 0x52
#define INPUT_KEY_KPDOT 0x53
#define INPUT_KEY_F11 0x57
#define INPUT_KEY_F12 0x58
#define INPUT_KEY_KPENTER 0x60
#define INPUT_KEY_RIGHTCTRL 0x61
#define INPUT_KEY_KPSLASH 0x62
#define INPUT_KEY_RIGHTALT 0x64
#define INPUT_KEY_HOME 0x66
#define INPUT_KEY_UP 0x67
#define INPUT_KEY_PAGEUP 0x68
#define INPUT_KEY_LEFT 0x69
#define INPUT_KEY_RIGHT 0x6a
#define INPUT_KEY_END 0x6b
#define INPUT_KEY_DOWN 0x6c
#define INPUT_KEY_PAGEDOWN 0x6d
#define INPUT_KEY_INSERT 0x6e
#define INPUT_KEY_DELETE 0x6f
#define INPUT_KEY_LEFTMETA 0x7d
#define INPUT_KEY_RIGHTMETA 0x7e
#define INPUT_KEY_F13 0xb7
#define INPUT_KEY_F14 0xb8
#define INPUT_KEY_F15 0xb9
#define INPUT_KEY_F16 0xba
#define INPUT_KEY_F17 0xbb
#define INPUT_KEY_F18 0xbc
#define INPUT_KEY_F19 0xbd
#define INPUT_KEY_F20 0xbe
#define INPUT_KEY_F21 0xbf
#define INPUT_KEY_F22 0xc0
#define INPUT_KEY_F23 0xc1
#define INPUT_KEY_F24 0xc2
#define INPUT_KEY_BTN_LEFT 0x110
#define INPUT_KEY_BTN_RIGHT 0x111
#define INPUT_KEY_BTN_MIDDLE 0x112
#define INPUT_KEY_BTN_SIDE 0x113
#define INPUT_KEY_BTN_EXTRA 0x114
#define INPUT_KEY_MAX 0x2ff
#define INPUT_KEY_CNT (INPUT_KEY_MAX + 1)

#define INPUT_REL_X 0x0
#define INPUT_REL_Y 0x1
#define INPUT_REL_WHEEL 0x8
#define INPUT_REL_MAX 0xf
#define INPUT_REL_CNT (INPUT_REL_MAX + 1)

#define INPUT_ABS_X 0x0
#define INPUT_ABS_Y 0x1
#define INPUT_ABS_MAX 0x3f
#define INPUT_ABS_CNT (INPUT_ABS_MAX + 1)

typedef struct input_device input_device_t;

typedef struct {
	list_node_t node;

	spinlock_t lock;
	pollheader_t poll_header;
	mutex_t mutex; // for userspace to not race
	ringbuffer_t buffer;

	input_device_t *device;
	vnode_t *vnode;
} input_listener_t;

struct input_device {
	spinlock_t lock;
	list_t listeners;

	int clock_id;

	char name[128];
	char phys[64];
	char uniq[64];

	bitmap_t prop_bits;
	bitmap_t ev_bits;
	bitmap_t syn_bits;
	bitmap_t key_bits;
	bitmap_t rel_bits;
	bitmap_t abs_bits;
	input_absinfo_t abs_info[INPUT_ABS_CNT];

	uint16_t id_bus;
	uint16_t id_vendor;
	uint16_t id_product;
	uint16_t id_version;

	uint16_t ver_major;
	uint8_t ver_minor;
	uint8_t ver_patch;
};

input_device_t *input_new();

// Posts the given input events to the device's event queue followed by a SYN_REPORT event.
void input_queue_packet(input_device_t *dev, input_event_t *events, int count);

#endif
