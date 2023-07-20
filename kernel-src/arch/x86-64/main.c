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
#include <kernel/alloc.h>
#include <arch/acpi.h>
#include <arch/apic.h>
#include <arch/hpet.h>
#include <kernel/timekeeper.h>
#include <kernel/scheduler.h>
#include <kernel/vfs.h>
#include <kernel/tmpfs.h>
#include <kernel/initrd.h>
#include <kernel/devfs.h>
#include <kernel/pseudodevices.h>
#include <arch/ps2.h>
#include <kernel/keyboard.h>
#include <kernel/console.h>
#include <kernel/dpc.h>
#include <kernel/pipefs.h>
#include <kernel/fb.h>
#include <kernel/pci.h>
#include <kernel/nvme.h>
#include <kernel/block.h>

static cpu_t bsp_cpu;

void kernel_entry() {
	cpu_set(&bsp_cpu);
	logging_sethook(arch_e9_putc);
	arch_gdt_reload();
	arch_idt_setup();
	arch_idt_reload();
	dpc_init();
	pmm_init();
	term_init();
	logging_sethook(term_putchar);
	arch_mmu_init();
	vmm_init();
	alloc_init();
	arch_acpi_init();
	arch_apic_init();
	cpu_initstate();
	// XXX fall back to another clock source
	time_t u = arch_hpet_init();
	timekeeper_init(arch_hpet_ticks, u);
	arch_apic_timerinit();
	sched_init();
	vfs_init();
	tmpfs_init();
	devfs_init();
	pipefs_init();
	printf("mounting tmpfs on /\n");
	__assert(vfs_mount(NULL, vfsroot, "/", "tmpfs", NULL) == 0);
	initrd_unpack();

	vattr_t attr = {0};
	attr.mode = 0644;
	__assert(vfs_create(vfsroot, "/dev", &attr, V_TYPE_DIR, NULL) == 0);

	printf("mounting devfs on /dev\n");
	__assert(vfs_mount(NULL, vfsroot, "/dev", "devfs", NULL) == 0);

	block_init();
	pseudodevices_init();
	pci_init();
	keyboard_init();
	arch_ps2_init();
	fb_init();
	nvme_init();

	console_init();
	logging_sethook(console_putc);

	sched_runinit();
	sched_threadexit();
}
