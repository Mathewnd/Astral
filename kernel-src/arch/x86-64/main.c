#include <arch/e9.h>
#include <logging.h>

void kernel_entry() {
	logging_sethook(arch_e9_putc);
	__assert(!"kernel entry end");
}
