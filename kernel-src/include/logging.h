#ifndef _LOGGING_H
#define _LOGGING_H

#include <printf.h>
#include <panic.h>

void _putchar(char c);
void logging_sethook(void (*fun)(char));

#define __assert(x) \
	if (!(x)){ \
		printf("\e[91m%s:%s:%d: %s failed\n\e[0m", __FILE__, __func__, __LINE__, #x); \
		_panic(NULL, NULL); \
	};


#endif
