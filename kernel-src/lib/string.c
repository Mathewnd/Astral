#include <string.h>
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *d, void *s, size_t c) {
        uint8_t *sc = (uint8_t *)s;
        uint8_t *dc = (uint8_t *)d;

        while (c--)
                *dc++ = *sc++;

        return d;
}

void *memset(void *ptr, unsigned long value, size_t num) {
        char *p = (char *)ptr;
        while (num--)
                *p++ = value;

        return ptr;
}

char *strcat(char *dest, const char *src) {
	dest += strlen(dest);

	strcpy(dest, src);

	return dest;
	
}
int strcmp(const char *a, const char *b) {
        while (*a && (*a == *b)) {
		++a;
		++b;
	}

        return *(const unsigned char *)a - *(const unsigned char *)b;
}

char *strcpy(char *p, const char *s) {
	const char *temp1 = s;
	char *temp2 = p;
	while (*temp1 != '\0') {
		*temp2 = *temp1;
		temp1++;
		temp2++;
	}
	*temp2 = '\0';
	return p;
}

size_t strlen(const char *a) {
        size_t s = 0;

        while (a[s] != '\0')
		++s;

        return s;
}

int strncmp(const char *a, const char *b, size_t c) {
        int diff = 0;

        while (c-- && diff == 0)
                diff += *a++ - *b++;

        return diff;
}

int memcmp(const void *_a, const void *_b, size_t s) {
	const uint8_t *a = _a;
	const uint8_t *b = _b;
	int diff = 0;

	while (s-- && diff == 0)
		diff += *a++ - *b++;

	return diff;
}
