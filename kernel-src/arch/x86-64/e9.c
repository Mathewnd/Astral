#include <arch/io.h>

// TODO compile time options for disabling output to port e9h

void arch_e9_putc(char c) {
	outb(0xe9, c);
}

void arch_e9_puts(char *c) {
	while (*c) {
		outb(0xe9, *c++);
	}
}
