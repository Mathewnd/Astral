#include <limine.h>
#include <kernel/console.h>
#include <arch/gdt.h>
#include <arch/idt.h>
#include <arch/cls.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/alloc.h>
#include <arch/panic.h>
#include <kernel/vfs.h>
#include <arch/acpi.h>
#include <arch/apic.h>
#include <kernel/initrd.h>
#include <arch/spinlock.h>
#include <stdio.h>
#include <arch/pci.h>
#include <arch/hpet.h>
#include <arch/smp.h>
#include <kernel/timer.h>
#include <kernel/keyboard.h>
#include <arch/timekeeper.h>

void kmain(){

	liminetty_init();

	bsp_setcls();

	gdt_init();

	idt_bspinit();
	
	pmm_init();	
	
	arch_mmu_init();
	
	vmm_init();	

	liminetty_setcontext(arch_getcls()->context);
	
	alloc_init();
	
	env_init();

	cmdline_parse();

	acpi_init();
	
	apic_init();
	
	apic_lapicinit();

	hpet_init();

	arch_timekeeper_init();

	timer_init();

	vfs_init();
	
	printf("Mounting tmpfs in /\n");

	if(vfs_mount(vfs_root(), NULL, "", "tmpfs", 0, NULL))
		_panic("Failed to mount tmpfs", 0);
	
	initrd_parse();
	
	devman_init();
	
	consoledev_init();

	pseudodevs_init();

	#ifdef USE_E9

	e9out_init();

	#endif
	
	fb_init();

	pci_enumerate();

	sched_init();

	cpu_state_init();

	smp_init();

	nvme_init();

	keyboard_init();
	
	mouse_init();

	ps2_init();
	
	sched_runinit();

	_panic("End of kmain()", 0);

}


