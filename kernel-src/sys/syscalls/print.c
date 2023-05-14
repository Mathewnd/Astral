#include <kernel/syscalls.h>
#include <printf.h>
#include <arch/e9.h>

syscallret_t syscall_print(context_t *context, char *message) {
	syscallret_t ret = {
		.ret = 0,
		.errno = 0
	};
	//printf("%s\n", message);
	arch_e9_puts(message);
	arch_e9_putc('\n');
	return ret;
}
