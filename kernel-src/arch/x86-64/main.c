#include <arch/e9.h>
#include <logging.h>
#include <arch/cpu.h>
#include <arch/gdt.h>
#include <arch/idt.h>
#include <kernel/term.h>
#include <kernel/pmm.h>
#include <arch/mmu.h>
#include <kernel/vmm.h>
#include <kernel/slab.h>

static cpu_t bsp_cpu;

void kernel_entry() {
	cpu_set(&bsp_cpu);
	logging_sethook(arch_e9_putc);
	arch_gdt_reload();
	arch_idt_setup();
	arch_idt_reload();
	pmm_init();
	term_init();
	logging_sethook(term_putchar);
	arch_mmu_init();
	vmm_init();
	slab_init();
	__assert(!"kernel entry end");
}
