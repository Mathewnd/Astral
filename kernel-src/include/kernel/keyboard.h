#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#define KBPACKET_FLAGS_RELEASED 1
#define KBPACKET_FLAGS_LEFTCTRL 2
#define KBPACKET_FLAGS_CAPSLOCK 4
#define KBPACKET_FLAGS_LEFTALT 8
#define KBPACKET_FLAGS_RIGHTALT 16
#define KBPACKET_FLAGS_LEFTSHIFT 32
#define KBPACKET_FLAGS_RIGHTCTRL 64
#define KBPACKET_FLAGS_RIGHTSHIFT 128

#define KEYCODE_RESERVED 0
#define KEYCODE_ESCAPE 1
#define KEYCODE_1 2
#define KEYCODE_2 3
#define KEYCODE_3 4
#define KEYCODE_4 5
#define KEYCODE_5 6
#define KEYCODE_6 7
#define KEYCODE_7 8
#define KEYCODE_8 9
#define KEYCODE_9 10
#define KEYCODE_0 11
#define KEYCODE_MINUS 12
#define KEYCODE_EQUAL 13
#define KEYCODE_BACKSPACE 14
#define KEYCODE_TAB 15
#define KEYCODE_Q 16
#define KEYCODE_W 17
#define KEYCODE_E 18
#define KEYCODE_R 19
#define KEYCODE_T 20
#define KEYCODE_Y 21
#define KEYCODE_U 22
#define KEYCODE_I 23
#define KEYCODE_O 24
#define KEYCODE_P 25
#define KEYCODE_LEFTBRACE 26
#define KEYCODE_RIGHTBRACE 27
#define KEYCODE_ENTER 28 
#define KEYCODE_LEFTCTRL 29
#define KEYCODE_A 30
#define KEYCODE_S 31
#define KEYCODE_D 32
#define KEYCODE_F 33
#define KEYCODE_G 34
#define KEYCODE_H 35
#define KEYCODE_J 36
#define KEYCODE_K 37
#define KEYCODE_L 38
#define KEYCODE_SEMICOLON 39
#define KEYCODE_APOSTROPHE 40
#define KEYCODE_GRAVE 41
#define KEYCODE_LEFTSHIFT 42
#define KEYCODE_BACKSLASH 43
#define KEYCODE_Z 44
#define KEYCODE_X 45
#define KEYCODE_C 46
#define KEYCODE_V 47
#define KEYCODE_B 48
#define KEYCODE_N 49
#define KEYCODE_M 50
#define KEYCODE_COMMA 51
#define KEYCODE_DOT 52
#define KEYCODE_SLASH 53
#define KEYCODE_RIGHTSHIFT 54
#define KEYCODE_KEYPADASTERISK 55
#define KEYCODE_LEFTALT 56
#define KEYCODE_SPACE 57
#define KEYCODE_CAPSLOCK 58
#define KEYCODE_F1 59
#define KEYCODE_F2 60
#define KEYCODE_F3 61
#define KEYCODE_F4 62
#define KEYCODE_F5 63
#define KEYCODE_F6 64
#define KEYCODE_F7 65
#define KEYCODE_F8 66
#define KEYCODE_F9 67
#define KEYCODE_F10 68
#define KEYCODE_NUMLOCK 69
#define KEYCODE_SCROLLLOCK 70
#define KEYCODE_KEYPAD7 71
#define KEYCODE_KEYPAD8 72
#define KEYCODE_KEYPAD9 73
#define KEYCODE_KEYPADMINUS 74
#define KEYCODE_KEYPAD4 75
#define KEYCODE_KEYPAD5 76
#define KEYCODE_KEYPAD6 77
#define KEYCODE_KEYPADPLUS 78
#define KEYCODE_KEYPAD1 79
#define KEYCODE_KEYPAD2 80
#define KEYCODE_KEYPAD3 81
#define KEYCODE_KEYPAD0 82
#define KEYCODE_KEYPADDOT 83
#define KEYCODE_F11 87
#define KEYCODE_F12 88
#define KEYCODE_KEYPADENTER 96
#define KEYCODE_RIGHTCTRL 97
#define KEYCODE_KEYPADSLASH 98
#define KEYCODE_SYSREQ 99
#define KEYCODE_RIGHTALT 100
#define KEYCODE_LINEFEED  101
#define KEYCODE_HOME 102
#define KEYCODE_UP 103
#define KEYCODE_PAGEUP 104
#define KEYCODE_LEFT 105
#define KEYCODE_RIGHT 106
#define KEYCODE_END 107
#define KEYCODE_DOWN 108
#define KEYCODE_PAGEDOWN 109
#define KEYCODE_INSERT 110
#define KEYCODE_DELETE 111

#include <spinlock.h>
#include <semaphore.h>
#include <ringbuffer.h>
#include <kernel/poll.h>

typedef uintmax_t keycode_t;

typedef struct {
	ringbuffer_t packetbuffer;
	semaphore_t semaphore;
	spinlock_t lock;
	int flags;
	pollheader_t pollheader;
} keyboard_t;

typedef struct {
	char ascii;
	keycode_t keycode;
	int flags;
} kbpacket_t;

extern keyboard_t *keyboard_console;

void keyboard_init();
keyboard_t *keyboard_new();
void keyboard_sendpacket(keyboard_t *keyboard, kbpacket_t *packet);
bool keyboard_get(keyboard_t *keyboard, kbpacket_t *packet);
int keyboard_wait(keyboard_t *keyboard, kbpacket_t *packet);

#endif
