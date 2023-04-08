#include <arch/e9.h>

void kernel_entry() {
	arch_e9_puts("Hello, world!\n");
	asm("cli");
	asm("hlt");
}
