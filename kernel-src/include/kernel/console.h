#ifndef _CONSOLE_H
#define _CONSOLE_H

#include <stddef.h>

void console_init();
void console_putc(char c);
void console_write(char *str, size_t size);

#endif
