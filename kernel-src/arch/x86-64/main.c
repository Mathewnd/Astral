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
#include <kernel/ext2.h>
#include <kernel/cmdline.h>

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
	cmdline_parse();
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
	ext2_init();

	block_init();
	pseudodevices_init();
	pci_init();
	keyboard_init();
	arch_ps2_init();
	fb_init();
	nvme_init();
	console_init();
	logging_sethook(console_putc);

	char *root   = cmdline_get("root");
	char *rootfs = cmdline_get("rootfs");

	printf("mounting %s (%s) on /\n", root == NULL ? "none" : root, rootfs);

	vnode_t *backing;
	if (root) {
		__assert(devfs_getbyname(root, &backing) == 0);
	} else {
		backing = NULL;
	}

	__assert(vfs_mount(backing, vfsroot, "/", rootfs, NULL) == 0);

	if (backing) {
		VOP_RELEASE(backing);
	}

	if (cmdline_get("initrd"))
		initrd_unpack();

	sched_runinit();
	sched_threadexit();
}
