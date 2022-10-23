#include <arch/io.h>

void e9_putc(char c){
	outb(0xE9, c);
}

void e9_puts(char* s){
	size_t len = strlen(s);

	for(uintmax_t i = 0; i < len; ++i)
		outb(0xE9, s[i]);
}
