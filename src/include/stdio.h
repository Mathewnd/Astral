#ifndef _STDIO_H_INCLUDE
#define _STDIO_H_INCLUDE

#include <stdarg.h>

int putchar(char c);
int printf(const char* format, ...);
int vprintf(const char* format, va_list arg);
int sprintf(char*, const char*, ...);
int vsprintf(char*, const char*, va_list arg);

#endif
