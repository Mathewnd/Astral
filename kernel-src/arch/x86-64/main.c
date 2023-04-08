#include <arch/e9.h>
#include <logging.h>
#include <arch/cpu.h>

static cpu_t bsp_cpu;

void kernel_entry() {
	cpu_set(&bsp_cpu);
	logging_sethook(arch_e9_putc);
	arch_gdt_reload();
	__assert(!"kernel entry end");
}
