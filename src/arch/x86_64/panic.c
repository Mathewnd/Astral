#include <arch/panic.h>
#include <stdio.h>

__attribute((noreturn)) void _panic(char* reason){
	
	// TODO stack trace and register print
	
	printf("Kernel panic.\nWhy: %s\nWhere: TODO\nWho: TODO\n", reason);

	while(1){
		asm("cli;hlt;");
	}

}
