#ifndef _LOGGING_H
#define _LOGGING_H

#include <printf.h>

void _putchar(char c);
void logging_sethook(void (*fun)(char));

// TODO panic

#define __assert(x) \
	if (!(x)){ \
		printf("%s:%s:%d: %s failed\n", __FILE__, __func__, __LINE__, #x); \
		for(;;); \
	};


#endif
