// console.c
//
// provides the kernel with an abstraction over other possible console drivers.
// for example, console_putc is called by putchar() from stdio.h
//

#include <kernel/console.h>
#include <stddef.h>

static  void(*writehook)(char*, size_t);

void console_setwritehook(void(*hook)(char*, size_t)){
	writehook = hook;
}

void console_putc(char c){

	writehook(&c, 1);

}
