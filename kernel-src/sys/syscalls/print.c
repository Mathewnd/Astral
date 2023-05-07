#include <kernel/syscalls.h>
#include <printf.h>

syscallret_t syscall_print(context_t *context, char *message) {
	syscallret_t ret = {
		.ret = 0,
		.errno = 0
	};
	printf("%s\n", message);
	return ret;
}
