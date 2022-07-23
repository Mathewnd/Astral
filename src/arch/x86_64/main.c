#include <limine.h>
#include <kernel/console.h>
#include <arch/gdt.h>
#include <arch/idt.h>
#include <arch/cls.h>
#include <kernel/pmm.h>

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

}


