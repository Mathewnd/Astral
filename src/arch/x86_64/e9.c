#include <arch/io.h>

void e9_putc(char c){
	#ifdef USE_E9 
	outb(0xE9, c);
	#endif
}

void e9_puts(char* s){
	#ifdef USE_E9 
	size_t len = strlen(s);

	for(uintmax_t i = 0; i < len; ++i)
		outb(0xE9, s[i]);
	#endif
}
