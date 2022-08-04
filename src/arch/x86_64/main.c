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

static volatile struct limine_terminal_request liminettyr = {
    .id = LIMINE_TERMINAL_REQUEST,
    .revision = 0
};

struct limine_terminal_response* liminetty;

void liminewrite(char* str, size_t c){
	
	liminetty->write(liminetty->terminals[0], str, c);

}

void kmain(){
	if(!liminettyr.response)
		return;

	liminetty = liminettyr.response;
	liminetty->write += 0xFFFF800000000000;
	
	console_setwritehook(liminewrite);
	
	bsp_setcls();
	gdt_bspinit();
	idt_bspinit();
	
	pmm_init();
	
	//set from 0xA0000 to 0x100000 as used
	//as sometimes the bootloader doesn't pass this region as being used

	pmm_setused((void*)0xA0000, 0x60);
	
	arch_mmu_init();
	
	vmm_init();	
	
	alloc_init();
	
	vfs_init();
	
	printf("Mounting tmpfs in /\n");

	if(vfs_mount(vfs_root(), NULL, "", "tmpfs", 0, NULL))
		_panic("Failed to mount tmpfs", 0);
	
	initrd_parse();

	_panic("End of kmain()", 0);

}


