#include <kernel/console.h>


int putchar(char c){
	
	console_putc(c);

	return c;
}
