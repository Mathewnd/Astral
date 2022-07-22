#ifndef _CONSOLE_H_INCLUDE
#define _CONSOLE_H_INCLUDE

#include <stddef.h>

void console_setwritehook(void(*hook)(char*, size_t));
void console_putc(char c);


#endif
