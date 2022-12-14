#ifndef _STRING_H_INCLUDE
#define _STRING_H_INCLUDE

#include <stddef.h>

size_t strlen(char* str);
char *strcpy(char *dest, const char* src);
char *strcat(char *dest, const char* str);
void *memcpy(void* dest, void* src, size_t size);
void *memset(void* dest, unsigned long what, size_t size);
int   strcmp(const char *a, const char* b);
int   strncmp(const char* a, const char* b, size_t c);
char* strerror(int errnum);

#endif 
