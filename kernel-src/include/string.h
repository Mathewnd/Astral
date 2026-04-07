#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

size_t strlen(const char *str);
size_t strnlen(const char *str, size_t max);
char *strcpy(char *dest, const char *src);
char *strcat(char *dest, const char *str);
void *memcpy(void *dest, const void *src, size_t size);
void *memset(void *dest, int what, size_t size);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t c);
int memcmp(const void *a, const void *b, size_t s);
void *memmove(void *_d, const void *_s, size_t c);
char *strerror(int errno);
long long atoll(const char *c);

#endif
