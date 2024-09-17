#include <kernel/syscalls.h>
#include <printf.h>
#include <arch/e9.h>
#include <mutex.h>

syscallret_t syscall_print(context_t *context, char *message) {
	syscallret_t ret = {
		.ret = 0,
		.errno = 0
	};

	static mutex_t mutex;
	static bool initialized = false;

	if (initialized == false) {
		MUTEX_INIT(&mutex);
		initialized = true;
	}

	MUTEX_ACQUIRE(&mutex, false);
	arch_e9_puts(message);
	arch_e9_putc('\n');
	MUTEX_RELEASE(&mutex);
	return ret;
}
